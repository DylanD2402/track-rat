/**
 * Host-side tests for TimeFormat.h.
 *
 *   g++ -std=c++17 -Iinclude test/test_timeformat.cpp -o /tmp/tf && /tmp/tf
 */

#include <cstring>
#include "TimeFormat.h"
#include "check.h"

static void lap(uint32_t ms, int dec, const char* want) {
  char b[24];
  timefmt::lapTime(b, sizeof(b), ms, dec);
  CHECK(!strcmp(b, want), "lapTime(%u, %d) = %s, want %s", (unsigned)ms, dec, b, want);
}
static void del(int32_t ms, const char* want) {
  char b[16];
  timefmt::delta(b, sizeof(b), ms);
  CHECK(!strcmp(b, want), "delta(%d) = %s, want %s", (int)ms, b, want);
}

int main() {
  lap(0, 3, "0:00.000");
  lap(83456, 3, "1:23.456");
  lap(83456, 1, "1:23.4");
  lap(83456, 0, "1:23");
  lap(3599999, 3, "59:59.999");
  lap(6000000, 3, "100:00.000");
  del(231, "+0.231");
  del(-1020, "-1.020");
  del(0, "+0.000");
  del(-5, "-0.005");
  del(12345, "+12.345");
  del(timefmt::NO_DELTA_MS, "--");
  del(INT32_MIN + 1, "-2147483.647");
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("test_timeformat: all passed\n");
  return 0;
}
