/**
 * Track Rat - u-blox UBX binary protocol reader
 *
 * UbxParser picks UBX frames (0xB5 0x62, class, id, length, payload,
 * 8-bit Fletcher checksum) out of a byte stream, one byte at a time. NMEA
 * text in between is skipped, and a frame split across several reads is
 * put back together. decodeNavPvt() unpacks a UBX-NAV-PVT payload: one
 * packet per fix with millisecond time, position, 3D velocity and the
 * receiver's own accuracy estimates.
 *
 * Pure C++ (no Arduino), tested on a PC in test/test_ubx.cpp.
 */

#ifndef UBX_PARSER_H
#define UBX_PARSER_H

#include <stdint.h>
#include <stddef.h>

constexpr uint8_t UBX_CLASS_NAV = 0x01;
constexpr uint8_t UBX_ID_NAV_PVT = 0x07;
constexpr uint16_t UBX_NAV_PVT_LEN = 92;

// Adds `len` bytes to a UBX frame's 8-bit Fletcher checksum (class, id,
// length and payload are covered). Start with a = b = 0.
inline void ubxChecksum(const uint8_t* data, size_t len, uint8_t& a, uint8_t& b) {
  for (size_t i = 0; i < len; i++) {
    a += data[i];
    b += a;
  }
}

class UbxParser {
public:
  // Larger frames are skipped. NAV-PVT is 92 bytes; CFG-VALGET replies
  // for a handful of keys are well under this.
  static constexpr uint16_t MAX_PAYLOAD = 100;

  // Feed one byte. Returns true when it completes a frame whose checksum
  // is good; the frame can then be read until the next feed().
  bool feed(uint8_t c);

  uint8_t msgClass() const { return cls; }
  uint8_t msgId() const { return id; }
  uint16_t length() const { return len; }
  const uint8_t* payload() const { return buf; }
  bool is(uint8_t c, uint8_t i) const { return cls == c && id == i; }
  // Part way through a frame (from its first sync byte to its last checksum byte)
  bool inFrame() const { return state != 0; }

  uint32_t framesOk() const { return okCount; }
  uint32_t checksumErrors() const { return badCount; }

private:
  uint8_t state = 0;
  uint8_t cls = 0, id = 0, ckA = 0, ckB = 0, rxCkA = 0;
  uint16_t len = 0, pos = 0;
  uint8_t buf[MAX_PAYLOAD];
  uint32_t okCount = 0, badCount = 0;
};

// UBX-NAV-PVT, raw units as sent by the receiver.
struct UbxPvt {
  uint32_t iTOW;        // GPS time of week of the navigation epoch, ms
  uint16_t year;        // UTC
  uint8_t month, day, hour, minute, second;
  uint8_t valid;        // bit 0 date valid, bit 1 time valid, bit 2 fully resolved
  uint32_t tAcc;        // time accuracy, ns
  int32_t nano;         // fraction of a second, ns (can be negative)
  uint8_t fixType;      // 0 none, 1 dead reckoning, 2 2D, 3 3D, 4 GNSS + DR, 5 time only
  uint8_t flags;        // bit 0 gnssFixOK, bit 5 headVehValid
  uint8_t flags2;
  uint8_t numSV;        // satellites used
  int32_t lon, lat;     // 1e-7 degrees
  int32_t height;       // above the ellipsoid, mm
  int32_t hMSL;         // above mean sea level, mm
  uint32_t hAcc, vAcc;  // mm
  int32_t velN, velE, velD;  // mm/s (velD positive = going down)
  int32_t gSpeed;       // ground speed, mm/s
  int32_t headMot;      // direction of travel, 1e-5 degrees
  uint32_t sAcc;        // speed accuracy, mm/s
  uint32_t headAcc;     // heading accuracy, 1e-5 degrees
  uint16_t pDOP;        // 0.01
  uint8_t flags3;       // bit 0 invalidLlh

  bool dateValid() const { return valid & 0x01; }
  bool timeValid() const { return valid & 0x02; }
  // A usable position: the receiver says the fix is good, it's 2D/3D
  // (with or without dead reckoning), and lat/lon aren't flagged invalid.
  bool fixOk() const {
    return (flags & 0x01) && fixType >= 2 && fixType <= 4 && !(flags3 & 0x01);
  }
  double latDeg() const { return lat * 1e-7; }
  double lonDeg() const { return lon * 1e-7; }
  float speedMs() const { return gSpeed * 0.001f; }
  float headingDeg() const { return headMot * 1e-5f; }
  float hAccM() const { return hAcc * 0.001f; }
  float vAccM() const { return vAcc * 0.001f; }
  float speedAccMs() const { return sAcc * 0.001f; }
  float headingAccDeg() const { return headAcc * 1e-5f; }
  float vertSpeedMs() const { return -velD * 0.001f; }  // + = climbing
  float altMslM() const { return hMSL * 0.001f; }
  float pdop() const { return pDOP * 0.01f; }

  // UTC milliseconds since midnight, rounded to the nearest ms.
  uint32_t utcMsOfDay() const;
};

// Unpacks a NAV-PVT payload. False if it's the wrong length.
bool decodeNavPvt(const uint8_t* payload, uint16_t len, UbxPvt& out);

// UTC ms since midnight from h:m:s plus a signed nanosecond fraction,
// rounded to the nearest ms and wrapped into 0..86399999.
uint32_t utcMsOfDay(uint8_t hour, uint8_t minute, uint8_t second, int32_t nano);

#endif  // UBX_PARSER_H
