// Host test for include/LocalTime.h: local file-name stamps from GPS UTC.
//   g++ -std=c++17 -Iinclude test/test_localtime.cpp -o /tmp/lt2 && /tmp/lt2

#include <cstdio>
#include <cstring>
#include "LocalTime.h"

static int failures = 0;
static void expect(int y, int mo, int d, int h, int mi, int s, int off, const char* want, const char* what) {
  char buf[24];
  localtime_::stamp(y, mo, d, h, mi, s, off, buf, sizeof(buf));
  if (strcmp(buf, want) != 0) {
    printf("FAIL %s: got %s, want %s\n", what, buf, want);
    failures++;
  }
}

int main() {
  printf("Local time stamp tests\n");
  expect(2026, 10, 18, 9, 30, 12, 0, "20261018_093012", "UTC unchanged");
  expect(2026, 10, 18, 13, 30, 12, -240, "20261018_093012", "Ontario summer");
  expect(2026, 10, 18, 2, 0, 0, -240, "20261017_220000", "previous day west of UTC");
  expect(2026, 12, 31, 23, 30, 0, 60, "20270101_003000", "new year east of UTC");
  expect(2027, 1, 1, 0, 15, 0, -300, "20261231_191500", "back over new year");
  expect(2028, 2, 28, 23, 0, 0, 120, "20280229_010000", "leap day");
  expect(2027, 2, 28, 23, 0, 0, 120, "20270301_010000", "no leap day");
  expect(2026, 3, 1, 0, 0, 0, -1, "20260228_235900", "one minute back over month end");
  expect(2026, 7, 1, 12, 0, 0, 330, "20260701_173000", "half-hour zone (India)");
  expect(2026, 7, 1, 12, 0, 0, -150, "20260701_093000", "Newfoundland summer");
  expect(2026, 7, 1, 12, 0, 0, 840, "20260702_020000", "UTC+14");
  expect(2026, 7, 1, 12, 0, 0, -720, "20260701_000000", "UTC-12");
  // The system clock's time and zone (for the SD card's file dates)
  {
    char tz[16];
    const struct { int off; const char* want; } zones[] = {
        {0, "UTC0"}, {-240, "LT+4"}, {-300, "LT+5"}, {-150, "LT+2:30"}, {330, "LT-5:30"}, {60, "LT-1"}, {840, "LT-14"}, {-720, "LT+12"}};
    for (const auto& z : zones) {
      localtime_::posixTz(z.off, tz, sizeof(tz));
      if (strcmp(tz, z.want) != 0) {
        printf("FAIL posixTz(%d) = %s, want %s\n", z.off, tz, z.want);
        failures++;
      }
    }
    if (localtime_::unixMs(1970, 1, 1, 0) != 0 || localtime_::unixMs(2026, 10, 20, 68414600) != 1792522814600LL) {
      printf("FAIL unixMs\n");
      failures++;
    }
  }
  for (int y = 2020; y <= 2100; y++) {  // round trip of every day
    for (int day = 0; day < 366; day++) {
      int32_t z = localtime_::daysFromCivil(y, 1, 1) + day;
      int yy, mm, dd;
      localtime_::civilFromDays(z, yy, mm, dd);
      if (localtime_::daysFromCivil(yy, mm, dd) != z) {
        printf("FAIL round trip %d+%d\n", y, day);
        failures++;
      }
    }
  }
  printf(failures ? "%d FAILED\n" : "ALL PASSED\n", failures);
  return failures ? 1 : 0;
}
