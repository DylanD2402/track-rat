/**
 * Track Rat - Firmware file check (no Arduino code, so it is tested on the PC)
 *
 * Looks at the first bytes of a .bin file and says whether it is an ESP32-S3
 * application image, so a wrong file (a bootloader, a merged flash dump, a
 * file for another chip, a text file) is refused instead of flashed.
 *
 * Layout of an app image (ESP-IDF esp_image_format.h):
 *   0   magic 0xE9, 1 segment count, 12 chip id (u16)
 *   24  first segment header (load address, length)
 *   32  esp_app_desc_t, which starts with the magic word 0xABCD5432; its project_name
 *       (at 80) must be "trackrat", so a program for the same chip from elsewhere is refused
 * The bootloader image has the same first 24 bytes but no app descriptor,
 * which is how the two are told apart.
 */

#ifndef FIRMWARE_IMAGE_H
#define FIRMWARE_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace firmware {

static const size_t HEADER_BYTES = 128;  // how much of the file is needed
static const uint8_t IMAGE_MAGIC = 0xE9;
static const uint16_t CHIP_ESP32S3 = 0x0009;
static const uint32_t APP_DESC_MAGIC = 0xABCD5432;
static const size_t APP_DESC_OFFSET = 32;
// esp_app_desc_t: magic 4, secure_version 4, reserv1 8, version 32, then project_name 32
static const size_t PROJECT_NAME_OFFSET = APP_DESC_OFFSET + 48;
static const size_t PROJECT_NAME_BYTES = 32;
static const char PROJECT_NAME[] = "trackrat";  // set after the link by tools/set_app_name.py

enum class Check { Ok, TooShort, TooBig, BadMagic, WrongChip, NotAnApp, NotTrackRat };

inline uint16_t readU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t readU32(const uint8_t* p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

// head: the first bytes of the file. fileSize: the whole file. slotSize: room in the app slot.
inline Check check(const uint8_t* head, size_t headLen, size_t fileSize, size_t slotSize) {
  if (headLen < HEADER_BYTES || fileSize < HEADER_BYTES) return Check::TooShort;
  if (fileSize > slotSize) return Check::TooBig;
  if (head[0] != IMAGE_MAGIC || head[1] == 0 || head[1] > 16) return Check::BadMagic;
  if (readU16(head + 12) != CHIP_ESP32S3) return Check::WrongChip;
  if (readU32(head + APP_DESC_OFFSET) != APP_DESC_MAGIC) return Check::NotAnApp;
  // Another ESP32-S3 program would install and run, but it isn't ours: the name must be exactly "trackrat"
  if (strncmp((const char*)head + PROJECT_NAME_OFFSET, PROJECT_NAME, PROJECT_NAME_BYTES) != 0) return Check::NotTrackRat;
  return Check::Ok;
}

// Wi-Fi upload: the file arrives in chunks of any size and nothing may be written to flash before
// the start of it has passed check(). The gate holds the first HEADER_BYTES, decides, and then
// says how much of the chunk that completed the header is left over to write.
class UploadGate {
public:
  void reset(size_t slotSize) {
    slot = slotSize;
    have = 0;
    done = false;
    result = Check::TooShort;
  }

  // Feed the next chunk. Returns how many of its bytes went into the header; the caller writes the
  // rest (data + returned, len - returned) after the header, once decided() and ok(). final = this
  // is the last chunk. The size of the whole file isn't known until then, so only the header's own
  // bytes are measured against the slot; Update refuses whatever doesn't fit as it arrives.
  size_t feed(const uint8_t* data, size_t len, bool final) {
    if (done) return 0;
    size_t take = HEADER_BYTES - have < len ? HEADER_BYTES - have : len;
    if (take) memcpy(head + have, data, take);
    have += take;
    if (have == HEADER_BYTES || final) {
      result = check(head, have, have, slot);
      done = true;
    }
    return take;
  }

  bool decided() const { return done; }
  bool ok() const { return done && result == Check::Ok; }
  Check outcome() const { return result; }
  const uint8_t* header() const { return head; }  // the bytes to write first
  size_t headerLen() const { return have; }

private:
  uint8_t head[HEADER_BYTES];
  size_t have = 0;
  size_t slot = 0;
  bool done = false;
  Check result = Check::TooShort;
};

enum class Copy { Ok, ReadError, WriteError };

// Copy `total` bytes from read() to write() in chunks, reporting progress. read(buf, want) returns the
// bytes read (0 = failed); write(buf, n) returns true if all n were written. A short or failed read or
// write stops the copy: a half-written image is never accepted.
template <class ReadFn, class WriteFn, class ProgressFn>
inline Copy copyImage(size_t total, uint8_t* buf, size_t bufSize, ReadFn read, WriteFn write, ProgressFn progress) {
  size_t done = 0;
  progress(done, total);
  while (done < total) {
    size_t want = total - done < bufSize ? total - done : bufSize;
    size_t got = read(buf, want);
    if (got == 0 || got > want) return Copy::ReadError;
    if (!write(buf, got)) return Copy::WriteError;
    done += got;
    progress(done, total);
  }
  return Copy::Ok;
}

inline const char* describe(Check c) {
  switch (c) {
    case Check::Ok: return "ok";
    case Check::TooShort: return "file is too small to be firmware";
    case Check::TooBig: return "file is too big for the app slot";
    case Check::BadMagic: return "not a firmware image";
    case Check::WrongChip: return "firmware is for a different chip";
    case Check::NotAnApp: return "not the app image (use firmware.bin)";
    case Check::NotTrackRat: return "not Track Rat firmware";
  }
  return "?";
}

}  // namespace firmware

#endif  // FIRMWARE_IMAGE_H
