/**
 * Host-side tests for the drive-off detector (starts a session from the Ready
 * screen once the car moves).
 *
 *   g++ -std=c++17 -Iinclude test/test_autostart.cpp -o /tmp/as && /tmp/as
 */

#include <cstdio>
#include <initializer_list>
#include "AutoStart.h"
#include "check.h"

static const uint32_t DAY_MS = 86400000UL;

// Feeds `seconds` of fixes at `hz`, all at `kmh`, from GPS time `*t` (which moves on).
// Returns the ms after the first fix at which the detector fired, or -1.
static long feed(DriveOffDetector& d, float kmh, double seconds, int hz, uint32_t* t) {
  const uint32_t step = 1000 / hz;
  const int n = (int)(seconds * hz);
  const uint32_t t0 = *t;
  long fired = -1;
  for (int i = 0; i < n; i++) {
    if (d.onFix(kmh, *t) && fired < 0) fired = (long)((*t + DAY_MS - t0) % DAY_MS);
    *t = (*t + step) % DAY_MS;
  }
  return fired;
}

static void testParked() {
  DriveOffDetector d;
  uint32_t t = 36000000;
  bool fired = false;
  for (int i = 0; i < 6000; i++) {  // 10 minutes of GPS noise at 10 Hz
    float kmh = (float)(testRand() % 61) / 10.0f;  // 0..6 km/h
    fired |= d.onFix(kmh, t);
    t += 100;
  }
  CHECK(!fired, "parked GPS noise fired");
}

static void testSpike() {
  DriveOffDetector d;
  uint32_t t = 1000;
  feed(d, 3, 5, 10, &t);
  CHECK(feed(d, 40, 1.0, 10, &t) < 0, "a 1 s spike fired");
  CHECK(feed(d, 3, 5, 10, &t) < 0, "fired after a spike");
  // A spike that is shorter than the hold, then a second one: the first doesn't count towards the second
  CHECK(feed(d, 40, 1.5, 10, &t) < 0, "1.5 s spike fired");
  feed(d, 3, 0.1, 10, &t);
  CHECK(feed(d, 40, 1.5, 10, &t) < 0, "two 1.5 s spikes fired");
}

static void testSteady() {
  DriveOffDetector d;
  uint32_t t = 5000;
  feed(d, 0, 10, 10, &t);
  long at = feed(d, 25, 5, 10, &t);
  CHECK(at >= 0, "steady 25 km/h never fired");
  CHECK(at >= (long)AUTO_START_HOLD_MS && at <= (long)AUTO_START_HOLD_MS + 150, "fired after %ld ms, want ~%d", at,
        (int)AUTO_START_HOLD_MS);
  // It stays fired while the car keeps going
  CHECK(d.onFix(30, t), "stopped reporting while still driving");
  // Exactly at the threshold counts
  d.reset();
  CHECK(feed(d, (float)AUTO_START_KMH, 3, 10, &t) >= 0, "exactly the threshold did not fire");
  // Just under it doesn't
  d.reset();
  CHECK(feed(d, (float)AUTO_START_KMH - 0.1f, 10, 10, &t) < 0, "just under the threshold fired");
}

static void testDropBelow() {
  DriveOffDetector d;
  uint32_t t = 100;
  CHECK(feed(d, 30, 1.8, 10, &t) < 0, "fired early");
  d.onFix(10, t);  // one slow fix
  t += 100;
  CHECK(feed(d, 30, 1.8, 10, &t) < 0, "one slow fix did not reset the count");
  long at = feed(d, 30, 1, 10, &t);
  CHECK(at >= 0, "never fired after the reset");
}

static void testGap() {
  DriveOffDetector d;
  uint32_t t = 50000;
  CHECK(feed(d, 30, 1.5, 10, &t) < 0, "fired early");
  t += 2500;  // GPS dropout, still fast on the other side
  CHECK(feed(d, 30, 1.5, 10, &t) < 0, "a gap over 2 s did not reset the count");
  long at = feed(d, 30, 1, 10, &t);
  CHECK(at >= 0, "never fired after the gap");

  // A gap of exactly MAX_FIX_GAP_MS is still the same run
  d.reset();
  t = 90000;
  d.onFix(30, t);
  t += MAX_FIX_GAP_MS;
  d.onFix(30, t);
  CHECK(d.onFix(30, t + 10), "a 2 s gap should keep counting");

  // A fix from the past (clock step) is a gap, not a huge forward jump
  d.reset();
  t = 200000;
  d.onFix(30, t);
  CHECK(!d.onFix(30, t - 5000), "a fix from the past fired");
}

static void testMidnight() {
  for (int hz : {10, 20}) {
    DriveOffDetector d;
    uint32_t t = DAY_MS - 1000;  // one second before midnight
    long at = feed(d, 25, 5, hz, &t);
    CHECK(at >= (long)AUTO_START_HOLD_MS && at <= (long)AUTO_START_HOLD_MS + 100, "across midnight at %d Hz: fired after %ld ms", hz, at);
  }
  // The wrap itself isn't a gap and doesn't make a huge step
  DriveOffDetector d;
  d.onFix(30, DAY_MS - 50);
  CHECK(!d.onFix(30, 50), "fired on the midnight step");
  uint32_t t = 150;
  CHECK(feed(d, 30, 1.5, 10, &t) < 0, "midnight step counted as a long time");
  // A gap that straddles midnight is still a gap
  d.reset();
  d.onFix(30, DAY_MS - 500);
  d.onFix(30, DAY_MS - 100);
  CHECK(!d.onFix(30, 3000), "gap across midnight fired");
}

static void testReset() {
  DriveOffDetector d;
  uint32_t t = 7000;
  CHECK(feed(d, 30, 5, 10, &t) >= 0, "never fired");
  d.reset();
  CHECK(!d.onFix(30, t), "fired right after reset()");
  t += 100;
  CHECK(feed(d, 30, 1.5, 10, &t) < 0, "reset() kept the old count");
  CHECK(feed(d, 30, 1, 10, &t) >= 0, "never fired after reset()");
}

static void testRates() {
  for (int hz : {10, 20}) {
    DriveOffDetector d;
    uint32_t t = 12345;
    feed(d, 2, 3, hz, &t);
    long at = feed(d, 22, 4, hz, &t);
    CHECK(at >= (long)AUTO_START_HOLD_MS && at <= (long)AUTO_START_HOLD_MS + 1000 / hz, "%d Hz: fired after %ld ms", hz, at);
    // Speed that wobbles around 26 but stays over the threshold still fires
    DriveOffDetector w;
    bool fired = false;
    t = 600000;
    for (int i = 0; i < 4 * hz; i++, t += 1000 / hz) fired |= w.onFix(25.0f + (float)(testRand() % 100) / 10.0f - 4.0f, t);
    CHECK(fired, "%d Hz: wobbly 21-31 km/h never fired", hz);
  }
}

int main() {
  testSeed(7);
  testParked();
  testSpike();
  testSteady();
  testDropBelow();
  testGap();
  testMidnight();
  testReset();
  testRates();
  if (failures) {
    printf("%d FAILED\n", failures);
    return 1;
  }
  printf("test_autostart: all passed\n");
  return 0;
}
