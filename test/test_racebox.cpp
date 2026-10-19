/**
 * Host-side tests for the RaceBox packet builder.
 *
 *   g++ -std=c++17 -Iinclude test/test_racebox.cpp src/RaceBoxPacket.cpp -o /tmp/rb && /tmp/rb
 */

#include <cstdio>
#include <cstring>
#include "RaceBoxPacket.h"

static int failures = 0;
#define CHECK(cond, ...)                         \
  do {                                           \
    if (!(cond)) {                               \
      failures++;                                \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                       \
      printf("\n");                              \
    }                                            \
  } while (0)

static uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int32_t i32(const uint8_t* p) { return (int32_t)u32(p); }
static uint16_t u16(const uint8_t* p) { return p[0] | p[1] << 8; }

int main() {
  printf("RaceBox packet tests\n");

  // GPS time of week. 2017-01-01 00:00:00 UTC is a Sunday, the start of a GPS week,
  // so TOW = 18 s of leap seconds.
  CHECK(gpsTimeOfWeekMs(2017, 1, 1, 0, 0, 0, 0) == 18000, "tow sunday %u", gpsTimeOfWeekMs(2017, 1, 1, 0, 0, 0, 0));
  // Saturday 2017-01-07 23:59:50 UTC -> GPS 2017-01-08 00:00:08, the next week
  CHECK(gpsTimeOfWeekMs(2017, 1, 7, 23, 59, 50, 0) == 8000, "tow wrap %u", gpsTimeOfWeekMs(2017, 1, 7, 23, 59, 50, 0));
  // Wednesday 2026-10-14 12:34:56.700 UTC: 3 days in
  uint32_t tow = gpsTimeOfWeekMs(2026, 10, 14, 12, 34, 56, 700);
  uint32_t expect = ((3 * 86400 + 12 * 3600 + 34 * 60 + 56 + 18) * 1000u) + 700;
  CHECK(tow == expect, "tow wed %u expected %u", tow, expect);
  printf("  GPS time of week: ok\n");

  RaceBoxFix f = {};
  f.dateValid = f.timeValid = true;
  f.year = 2026; f.month = 10; f.day = 14; f.hour = 12; f.minute = 34; f.second = 56; f.millis = 700;
  f.hasFix = true;
  f.satellites = 11;
  f.lat = 45.3017778;
  f.lon = -76.6715556;
  f.altMslM = 152.5f;
  f.speedMs = 33.333f;
  f.headingDeg = 271.25f;
  f.headingValid = true;
  f.hdop = 0.9f;
  f.gZ = 1000;
  f.batteryPercent = 100;

  uint8_t pk[RACEBOX_PACKET_LEN];
  buildRaceBoxPacket(f, pk);
  const uint8_t* p = pk + 6;

  CHECK(pk[0] == 0xB5 && pk[1] == 0x62 && pk[2] == 0xFF && pk[3] == 0x01, "header");
  CHECK(u16(pk + 4) == 80, "length %u", u16(pk + 4));
  uint8_t a = 0, b = 0;
  for (int i = 2; i < 86; i++) { a += pk[i]; b += a; }
  CHECK(pk[86] == a && pk[87] == b, "checksum");

  CHECK(u32(p + 0) == expect, "iTOW");
  CHECK(u16(p + 4) == 2026 && p[6] == 10 && p[7] == 14 && p[8] == 12 && p[9] == 34 && p[10] == 56, "date/time");
  CHECK(p[11] == 0x07, "validity flags %02X", p[11]);
  CHECK(i32(p + 16) == 700000000, "nano %d", i32(p + 16));
  CHECK(p[20] == 3, "fix type %u", p[20]);
  CHECK(p[21] == 0x21, "fix flags %02X", p[21]);
  CHECK(p[22] == 0xE0, "date/time flags %02X", p[22]);
  CHECK(p[23] == 11, "sats");
  CHECK(i32(p + 24) == -766715556, "lon %d", i32(p + 24));
  CHECK(i32(p + 28) == 453017778, "lat %d", i32(p + 28));
  CHECK(i32(p + 36) == 152500, "MSL %d", i32(p + 36));
  CHECK(i32(p + 48) == 33333, "speed %d", i32(p + 48));
  CHECK(i32(p + 52) == 27125000, "heading %d", i32(p + 52));
  CHECK(u16(p + 64) == 90, "pdop %u", u16(p + 64));
  CHECK(p[66] == 0, "latlon flags");
  CHECK(p[67] == 100, "battery");
  CHECK((int16_t)u16(p + 72) == 1000, "gZ");
  printf("  packet fields and checksum: ok\n");

  // No accuracy figures (NMEA): estimated from HDOP 0.9
  CHECK(u32(p + 40) == 2250 && u32(p + 44) == 3600, "estimated hAcc/vAcc %u %u", u32(p + 40), u32(p + 44));
  CHECK(u32(p + 56) == 200 && u32(p + 60) == 100000, "estimated sAcc/headAcc %u %u", u32(p + 56), u32(p + 60));
  printf("  accuracy estimated from HDOP: ok\n");

  // The GPS's own accuracy figures (UBX-NAV-PVT) go in as they are
  f.accuracyKnown = true;
  f.fixType = 3;
  f.hAccM = 1.234f;
  f.vAccM = 2.1f;
  f.speedAccMs = 0.18f;
  f.headingAccDeg = 0.52f;
  f.hdop = 1.35f;  // PDOP
  buildRaceBoxPacket(f, pk);
  CHECK(u32(p + 40) == 1234 && u32(p + 44) == 2100, "hAcc/vAcc %u %u", u32(p + 40), u32(p + 44));
  CHECK(u32(p + 56) == 180 && u32(p + 60) == 52000, "sAcc/headAcc %u %u", u32(p + 56), u32(p + 60));
  CHECK(u16(p + 64) == 135 && p[20] == 3, "pdop %u fix %u", u16(p + 64), p[20]);
  f.fixType = 2;
  f.hAccM = 5e6f;  // absurd: capped, not wrapped
  buildRaceBoxPacket(f, pk);
  CHECK(p[20] == 2, "2D fix type %u", p[20]);
  CHECK(u32(p + 40) == 0xFFFFFFFF, "capped hAcc %u", u32(p + 40));
  f.fixType = 4;  // GNSS + dead reckoning: RaceBox has no such type, reported as 3D
  buildRaceBoxPacket(f, pk);
  CHECK(p[20] == 3, "GNSS+DR fix type %u", p[20]);
  f.fixType = 2;
  a = b = 0;
  for (int i = 2; i < 86; i++) { a += pk[i]; b += a; }
  CHECK(pk[86] == a && pk[87] == b, "checksum");
  printf("  real accuracy figures: ok\n");

  // No fix: position flagged invalid, fix type 0
  f.hasFix = false;
  f.satellites = 0;
  buildRaceBoxPacket(f, pk);
  CHECK(p[20] == 0 && (p[21] & 1) == 0 && p[66] == 1, "no-fix flags %u %02X %u", p[20], p[21], p[66]);
  printf("  no-fix packet: ok\n");

  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
