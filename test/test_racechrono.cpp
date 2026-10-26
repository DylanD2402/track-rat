/**
 * Host-side tests for RaceChrono's DIY Bluetooth LE API (RaceChrono.h, 6.3):
 * latitude and longitude, both altitude and speed modes and where they switch,
 * bearing, HDOP, the time since the hour and the date-hour, the sync bits,
 * invalid values without a fix, CAN packets (the little-endian ID), the value
 * packets' equations, and the filter commands. Untested with RaceChrono.
 *
 *   g++ -std=c++17 -Iinclude test/test_racechrono.cpp -o /tmp/rc && /tmp/rc
 */

#include <cmath>
#include <cstdio>
#include "RaceChrono.h"
#include "check.h"

using namespace rc;

static uint32_t u16(const uint8_t* p) { return p[0] << 8 | p[1]; }
static int32_t s32(const uint8_t* p) { return (int32_t)((uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); }

int main() {
  printf("RaceChrono tests\n");

  Fix f;
  f.valid = true;
  f.year = 2026, f.month = 10, f.day = 20;
  f.utcMs = ((14 * 60 + 23) * 60 + 45) * 1000 + 678;  // 14:23:45.678
  f.sats = 21;
  f.lat = 45.2987654, f.lon = -76.6123456;
  f.altM = 152.4f, f.speedKmh = 187.65f, f.courseDeg = 271.5f, f.courseValid = true, f.hdop = 0.7f;

  // GPS main
  {
    uint8_t o[20];
    gpsMain(f, 5, o);
    const uint32_t t = (uint32_t)(o[0] & 0x1F) << 16 | o[1] << 8 | o[2];
    CHECK(o[0] >> 5 == 5 && t == 23 * 30000 + 45 * 500 + 678 / 2, "sync 5, time from the hour %u", (unsigned)t);
    CHECK(o[3] >> 6 == 1 && (o[3] & 0x3F) == 21, "fix quality 1, 21 satellites");
    CHECK(s32(o + 4) == 452987654 && s32(o + 8) == -766123456, "lat %d lon %d", s32(o + 4), s32(o + 8));
    CHECK(u16(o + 12) == 6524, "altitude (152.4 + 500) x 10 = %u", (unsigned)u16(o + 12));
    CHECK(u16(o + 14) == 18765, "speed 187.65 x 100 = %u", (unsigned)u16(o + 14));
    CHECK(u16(o + 16) == 27150 && o[18] == 7 && o[19] == 0xFF, "bearing, HDOP, no VDOP");

    // Ranges: the fine forms up to their 15 bits, then the coarse ones with the top bit
    CHECK(altitudeField(2776.7f) == 0x7FFF && altitudeField(2776.8f) == (0x8000 | 3277), "altitude switches at 2776.7 m");
    CHECK(altitudeField(-500) == 0 && altitudeField(-600) == 0 && altitudeField(-12.3f) == 4877, "below sea level");
    CHECK(speedField(327.67f) == 32767 && speedField(327.7f) == (0x8000 | 3277) && speedField(0) == 0, "speed switches at 327.67 km/h");
    CHECK(speedField(412.3f) == (0x8000 | 4123), "400 km/h in tenths");

    // Many satellites, hdop unknown, course not valid, a negative course
    Fix g = f;
    g.sats = 70, g.hdop = -1, g.courseValid = false;
    gpsMain(g, 0, o);
    CHECK((o[3] & 0x3F) == 0x3F && o[18] == 0xFF && u16(o + 16) == 0xFFFF, "clamped and invalid values");
    g.courseValid = true, g.courseDeg = -90;
    gpsMain(g, 0, o);
    CHECK(u16(o + 16) == 27000, "-90 deg is 270");
    g.courseDeg = 359.999f;
    gpsMain(g, 0, o);
    CHECK(u16(o + 16) == 0, "never 360.00");

    // No fix: the invalid values
    Fix none;
    none.utcMs = 1000;
    gpsMain(none, 0, o);
    CHECK(o[3] >> 6 == 0 && s32(o + 4) == 0x7FFFFFFF && s32(o + 8) == 0x7FFFFFFF && u16(o + 12) == 0xFFFF && u16(o + 14) == 0xFFFF,
          "no fix: invalid position, altitude and speed");
  }

  // GPS time, and the sync bits
  {
    uint8_t o[3];
    gpsTime(f, 5, o);
    const uint32_t dh = (uint32_t)(o[0] & 0x1F) << 16 | o[1] << 8 | o[2];
    CHECK(o[0] >> 5 == 5 && dh == 26u * 8928 + 9 * 744 + 19 * 24 + 14, "date and hour %u", (unsigned)dh);
    CHECK(dateHour(2000, 1, 1, 0) == 0 && dateHour(2099, 12, 31, 23) < (1u << 21), "fits 21 bits");

    Sync s;
    const uint8_t a = s.update(dateHour(2026, 10, 20, 14));
    CHECK(s.update(dateHour(2026, 10, 20, 14)) == a, "same hour: same bits");
    CHECK(s.update(dateHour(2026, 10, 20, 15)) == ((a + 1) & 7), "next hour: up one");
    uint8_t b = 0;
    for (int h = 16; h < 24; h++) b = s.update(dateHour(2026, 10, 20, (uint8_t)h));
    CHECK(b == ((a + 9) & 7), "wraps round in 3 bits: %u", b);
  }

  // CAN packets
  {
    uint8_t o[20];
    const uint8_t pl[3] = {1, 2, 3};
    CHECK(canPacket(0x12345678, pl, 3, o) == 7 && o[0] == 0x78 && o[1] == 0x56 && o[2] == 0x34 && o[3] == 0x12 && o[4] == 1 && o[6] == 3,
          "little-endian ID, then the payload");
    CHECK(canPacket(1, pl, 0, o) == 0 && canPacket(1, pl, 17, o) == 0, "1 to 16 bytes only");
    CHECK(valuePacket(ID_RPM, 6512.4f, 1, o) == 6 && o[0] == 0x00 && o[1] == 0x01 && u16(o + 4) == 6512, "RPM");
    valuePacket(ID_COOLANT, -12.3f, 10, o);
    CHECK((int16_t)u16(o + 4) == -123, "coolant -12.3 C x 10");
    valuePacket(ID_LAT_G, 1.234f, 1000, o);
    CHECK((int16_t)u16(o + 4) == 1234 && o[0] == 0x10 && o[1] == 0x01, "lateral g, ID 0x110");
    valuePacket(ID_THROTTLE, 5000, 10, o);
    CHECK((int16_t)u16(o + 4) == 32767, "clamped");
  }

  // The filter
  {
    Filter fl;
    CHECK(!fl.due(ID_RPM, 0), "nothing before the app's filter");
    const uint8_t allowAll[] = {1, 0x00, 0x64};  // every 100 ms
    CHECK(fl.apply(allowAll, 3), "allow all");
    CHECK(fl.due(ID_RPM, 1000) && !fl.due(ID_RPM, 1050) && fl.due(ID_RPM, 1100), "at its interval");
    CHECK(fl.due(ID_COOLANT, 1050), "each ID on its own clock");
    const uint8_t deny[] = {0};
    const uint8_t one[] = {2, 0x01, 0xF4, 0x00, 0x00, 0x01, 0x02};  // 0x102 every 500 ms
    CHECK(fl.apply(deny, 1) && fl.apply(one, 7), "deny all, then one");
    CHECK(!fl.due(ID_RPM, 5000), "denied");
    CHECK(fl.due(ID_COOLANT, 5000) && !fl.due(ID_COOLANT, 5499) && fl.due(ID_COOLANT, 5500), "the allowed one at 500 ms");
    const uint8_t again[] = {2, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02};  // the same ID, every time
    CHECK(fl.apply(again, 7) && fl.due(ID_COOLANT, 5501), "interval updated");
    const uint8_t bad1[] = {1, 0};
    const uint8_t bad2[] = {7};
    CHECK(!fl.apply(bad1, 2) && !fl.apply(bad2, 1) && !fl.apply(bad2, 0), "wrong lengths and commands ignored");
    for (uint32_t id = 0; id < 20; id++) {
      const uint8_t c[] = {2, 0, 10, 0, 0, 0, (uint8_t)id};
      fl.apply(c, 7);
    }
    fl.reset();
    CHECK(!fl.due(ID_COOLANT, 9000), "reset when the app goes");
  }

  if (failures) {
    printf("%d FAILED\n", failures);
    return 1;
  }
  printf("All RaceChrono tests passed\n");
  return 0;
}
