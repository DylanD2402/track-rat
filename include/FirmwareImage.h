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
 *   32  esp_app_desc_t, which starts with the magic word 0xABCD5432
 * The bootloader image has the same first 24 bytes but no app descriptor,
 * which is how the two are told apart.
 */

#ifndef FIRMWARE_IMAGE_H
#define FIRMWARE_IMAGE_H

#include <stddef.h>
#include <stdint.h>

namespace firmware {

static const size_t HEADER_BYTES = 128;  // how much of the file is needed
static const uint8_t IMAGE_MAGIC = 0xE9;
static const uint16_t CHIP_ESP32S3 = 0x0009;
static const uint32_t APP_DESC_MAGIC = 0xABCD5432;
static const size_t APP_DESC_OFFSET = 32;

enum class Check { Ok, TooShort, TooBig, BadMagic, WrongChip, NotAnApp };

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
  return Check::Ok;
}

inline const char* describe(Check c) {
  switch (c) {
    case Check::Ok: return "ok";
    case Check::TooShort: return "file is too small to be firmware";
    case Check::TooBig: return "file is too big for the app slot";
    case Check::BadMagic: return "not a firmware image";
    case Check::WrongChip: return "firmware is for a different chip";
    case Check::NotAnApp: return "not the app image (use firmware.bin)";
  }
  return "?";
}

}  // namespace firmware

#endif  // FIRMWARE_IMAGE_H
