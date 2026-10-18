/**
 * Track Rat - RaceBox Mini data packet implementation
 *
 * Payload layout (little-endian), offsets in bytes:
 *   0 iTOW u32 ms        4 year u16      6 month   7 day   8 hour  9 min  10 sec
 *  11 validity flags    12 tAcc u32 ns  16 nano i32       20 fix type
 *  21 fix flags         22 date/time flags                23 satellites
 *  24 lon i32 1e-7 deg  28 lat i32      32 WGS height i32 mm   36 MSL height i32 mm
 *  40 hAcc u32 mm       44 vAcc u32 mm  48 speed i32 mm/s      52 heading i32 1e-5 deg
 *  56 speed acc u32     60 heading acc u32                     64 PDOP u16 0.01
 *  66 lat/lon flags     67 battery      68 gX gY gZ i16 milli-g    74 rX rY rZ i16 centi-deg/s
 */

#include "RaceBoxPacket.h"
#include <math.h>
#include <string.h>

static void put8(uint8_t* p, size_t off, uint8_t v) { p[off] = v; }
static void put16(uint8_t* p, size_t off, uint16_t v) {
  p[off] = v & 0xFF;
  p[off + 1] = v >> 8;
}
static void put32(uint8_t* p, size_t off, uint32_t v) {
  for (int i = 0; i < 4; i++) p[off + i] = (v >> (8 * i)) & 0xFF;
}

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
static int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

uint32_t gpsTimeOfWeekMs(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
                         uint8_t second, uint16_t millis) {
  const int64_t LEAP_SECONDS = 18;  // GPS - UTC since 2017
  int64_t days = daysFromCivil(year, month, day) - daysFromCivil(1980, 1, 6);  // GPS epoch, a Sunday
  int64_t secs = days * 86400 + hour * 3600 + minute * 60 + second + LEAP_SECONDS;
  int64_t ms = secs * 1000 + millis;
  const int64_t WEEK_MS = 604800000LL;
  return (uint32_t)(((ms % WEEK_MS) + WEEK_MS) % WEEK_MS);
}

void buildRaceBoxPacket(const RaceBoxFix& f, uint8_t* out) {
  uint8_t p[RACEBOX_PAYLOAD_LEN];
  memset(p, 0, sizeof(p));

  bool dateTime = f.dateValid && f.timeValid;
  put32(p, 0, dateTime ? gpsTimeOfWeekMs(f.year, f.month, f.day, f.hour, f.minute, f.second, f.millis) : 0);
  put16(p, 4, f.year);
  put8(p, 6, f.month);
  put8(p, 7, f.day);
  put8(p, 8, f.hour);
  put8(p, 9, f.minute);
  put8(p, 10, f.second);
  put8(p, 11, (f.dateValid ? 0x01 : 0) | (f.timeValid ? 0x02 : 0) | (dateTime ? 0x04 : 0));
  put32(p, 12, f.timeValid ? 50 : 0xFFFFFFFF);  // time accuracy, ns (estimate)
  put32(p, 16, (uint32_t)(int32_t)(f.millis * 1000000L));

  bool fix3d = f.hasFix && f.satellites >= 4;
  put8(p, 20, fix3d ? 3 : (f.hasFix ? 2 : 0));
  put8(p, 21, (f.hasFix ? 0x01 : 0) | (f.headingValid ? 0x20 : 0));
  put8(p, 22, (f.timeValid ? 0x20 : 0) | (f.dateValid ? 0x40 : 0) | (dateTime ? 0x80 : 0));
  put8(p, 23, f.satellites);

  put32(p, 24, (uint32_t)(int32_t)lround(f.lon * 1e7));
  put32(p, 28, (uint32_t)(int32_t)lround(f.lat * 1e7));
  int32_t altMm = (int32_t)lroundf(f.altMslM * 1000.0f);
  put32(p, 32, (uint32_t)altMm);  // NMEA has no ellipsoid height here; MSL used for both
  put32(p, 36, (uint32_t)altMm);

  // NMEA gives HDOP, not accuracy: estimate it with a typical 2.5 m range error.
  float hdop = f.hdop > 0 && f.hdop < 50 ? f.hdop : 50;
  put32(p, 40, f.hasFix ? (uint32_t)(hdop * 2500.0f) : 0xFFFFFFFF);
  put32(p, 44, f.hasFix ? (uint32_t)(hdop * 4000.0f) : 0xFFFFFFFF);
  put32(p, 48, (uint32_t)(int32_t)lroundf(f.speedMs * 1000.0f));
  float heading = fmodf(f.headingDeg, 360.0f);
  if (heading < 0) heading += 360.0f;
  put32(p, 52, (uint32_t)(int32_t)lround(heading * 1e5));
  put32(p, 56, 200);                                           // speed accuracy, mm/s (estimate)
  put32(p, 60, f.headingValid ? 100000 : 18000000);            // heading accuracy, 1e-5 deg
  put16(p, 64, (uint16_t)lroundf(hdop * 100.0f));              // PDOP ~ HDOP
  put8(p, 66, f.hasFix ? 0 : 0x01);                            // bit 0: position invalid
  put8(p, 67, f.batteryPercent);
  put16(p, 68, (uint16_t)f.gX);
  put16(p, 70, (uint16_t)f.gY);
  put16(p, 72, (uint16_t)f.gZ);
  put16(p, 74, (uint16_t)f.rX);
  put16(p, 76, (uint16_t)f.rY);
  put16(p, 78, (uint16_t)f.rZ);

  out[0] = 0xB5;
  out[1] = 0x62;
  out[2] = 0xFF;  // class: RaceBox
  out[3] = 0x01;  // id: data message
  out[4] = RACEBOX_PAYLOAD_LEN & 0xFF;
  out[5] = RACEBOX_PAYLOAD_LEN >> 8;
  memcpy(out + 6, p, RACEBOX_PAYLOAD_LEN);

  uint8_t a = 0, b = 0;  // 8-bit Fletcher over class..payload
  for (size_t i = 2; i < 6 + RACEBOX_PAYLOAD_LEN; i++) {
    a += out[i];
    b += a;
  }
  out[86] = a;
  out[87] = b;
}
