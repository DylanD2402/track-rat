/**
 * Track Rat - u-blox UBX binary protocol reader implementation
 *
 * NAV-PVT payload layout (little-endian), offsets in bytes:
 *   0 iTOW u32 ms        4 year u16      6 month   7 day   8 hour  9 min  10 sec
 *  11 valid             12 tAcc u32 ns  16 nano i32       20 fixType
 *  21 flags             22 flags2       23 numSV
 *  24 lon i32 1e-7 deg  28 lat i32      32 height i32 mm   36 hMSL i32 mm
 *  40 hAcc u32 mm       44 vAcc u32 mm  48 velN i32 mm/s   52 velE   56 velD
 *  60 gSpeed i32 mm/s   64 headMot i32 1e-5 deg            68 sAcc u32 mm/s
 *  72 headAcc u32 1e-5 deg              76 pDOP u16 0.01   78 flags3
 *  79-91 reserved, vehicle heading, magnetic declination (not used)
 */

#include "UbxParser.h"

bool UbxParser::feed(uint8_t c) {
  switch (state) {
    case 0:  // waiting for sync char 1
      if (c == 0xB5) state = 1;
      return false;
    case 1:  // sync char 2
      state = c == 0x62 ? 2 : (c == 0xB5 ? 1 : 0);
      return false;
    case 2:
      cls = c;
      ckA = ckB = 0;
      break;
    case 3: id = c; break;
    case 4: len = c; break;
    case 5:
      len |= c << 8;
      pos = 0;
      if (len > MAX_PAYLOAD) {  // too big for us (or a false sync): look for the next frame
        state = 0;
        return false;
      }
      break;
    case 6:
      buf[pos++] = c;
      break;
    case 7:
      rxCkA = c;
      state = 8;
      return false;
    case 8:
      state = 0;
      if (rxCkA == ckA && c == ckB) {
        okCount++;
        return true;
      }
      badCount++;
      return false;
  }

  // class, id, length and payload bytes are covered by the checksum
  ckA += c;
  ckB += ckA;
  if (state == 5 || state == 6) state = pos < len ? 6 : 7;
  else state++;
  return false;
}

static uint16_t u16(const uint8_t* p) { return p[0] | p[1] << 8; }
static uint32_t u32(const uint8_t* p) {
  return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int32_t i32(const uint8_t* p) { return (int32_t)u32(p); }

bool decodeNavPvt(const uint8_t* p, uint16_t len, UbxPvt& o) {
  if (len != UBX_NAV_PVT_LEN) return false;
  o.iTOW = u32(p + 0);
  o.year = u16(p + 4);
  o.month = p[6];
  o.day = p[7];
  o.hour = p[8];
  o.minute = p[9];
  o.second = p[10];
  o.valid = p[11];
  o.tAcc = u32(p + 12);
  o.nano = i32(p + 16);
  o.fixType = p[20];
  o.flags = p[21];
  o.flags2 = p[22];
  o.numSV = p[23];
  o.lon = i32(p + 24);
  o.lat = i32(p + 28);
  o.height = i32(p + 32);
  o.hMSL = i32(p + 36);
  o.hAcc = u32(p + 40);
  o.vAcc = u32(p + 44);
  o.velN = i32(p + 48);
  o.velE = i32(p + 52);
  o.velD = i32(p + 56);
  o.gSpeed = i32(p + 60);
  o.headMot = i32(p + 64);
  o.sAcc = u32(p + 68);
  o.headAcc = u32(p + 72);
  o.pDOP = u16(p + 76);
  o.flags3 = p[78];
  return true;
}

uint32_t utcMsOfDay(uint8_t hour, uint8_t minute, uint8_t second, int32_t nano) {
  const int64_t DAY_MS = 86400000LL;
  // nano is -1e9..1e9; round to the nearest ms (floor of nano/1e6 + 0.5)
  int64_t n = (int64_t)nano + 500000;
  int64_t fracMs = n >= 0 ? n / 1000000 : -((-n + 999999) / 1000000);
  int64_t ms = (hour * 3600LL + minute * 60LL + second) * 1000 + fracMs;
  return (uint32_t)(((ms % DAY_MS) + DAY_MS) % DAY_MS);
}

uint32_t UbxPvt::utcMsOfDay() const { return ::utcMsOfDay(hour, minute, second, nano); }
