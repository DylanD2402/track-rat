/**
 * Host-side tests for the screen that follows the situation (SituationScreen.h):
 * the slow screen, the pit lane, and the session clock.
 *
 *   g++ -std=c++17 -Iinclude test/test_situation.cpp -o /tmp/sit && /tmp/sit
 */

#include <cstdio>
#include <string>
#include "SituationScreen.h"
#include "check.h"

using namespace situation;

static void testSlow() {
  SlowWatch w;
  uint32_t t = 36000000;
  // At speed: never slow
  for (int i = 0; i < 100; i++, t += 50) CHECK(!w.onFix(120, t, false), "fast");
  // A hairpin: 55 km/h is above the threshold
  for (int i = 0; i < 100; i++, t += 50) CHECK(!w.onFix(55, t, false), "hairpin");
  // A slow corner for 3 s: not long enough
  for (int i = 0; i < 60; i++, t += 50) w.onFix(35, t, false);
  CHECK(!w.slow(), "3 s slow is a corner, not slow");
  w.onFix(80, t, false);
  // Behind the safety car, 30 km/h: slow after 5 s
  uint32_t start = t;
  bool became = false;
  uint32_t at = 0;
  for (int i = 0; i < 200; i++, t += 50)
    if (w.onFix(30, t, false) && !became) became = true, at = t;
  CHECK(became && at - start >= SLOW_SCREEN_MS && at - start <= SLOW_SCREEN_MS + 50, "slow after 5 s, got %u ms",
        (unsigned)(at - start));
  // Speeding up: timing at once
  CHECK(!w.onFix(SLOW_SCREEN_KMH, t, false), "back to timing at the threshold");
  // The pit lane: slow at once, at any speed (the limit there is often over 40 km/h in Europe, 60 in places)
  CHECK(w.onFix(60, t += 50, true), "in the pit lane: slow at once");
  CHECK(w.onFix(80, t += 50, true), "still in the pit lane");
  CHECK(!w.onFix(80, t += 50, false), "out of the pit lane and quick: timing");
  // Leaving the pit lane slowly: still slow, the timer doesn't start again
  w.onFix(30, t += 50, true);
  CHECK(w.onFix(30, t += 50, false), "out of the pit lane but slow: stays slow");
  // Across midnight
  SlowWatch m;
  t = 86400000 - 2000;
  for (int i = 0; i < 120; i++) {
    m.onFix(20, t, false);
    t = (t + 50) % 86400000;
  }
  CHECK(m.slow(), "6 s across midnight");
  m.reset();
  CHECK(!m.slow(), "reset");
  printf("  slow screen: 5 s under 40 km/h, at once in the pit lane, back when quick\n");
}

static void testPitLane() {
  // The main straight along y = 0, the pit lane parallel 15 m south, and a corner
  layout::Lines map = {
      {{-300, 0}, {300, 0}, {350, 50}},
      {{-250, -15}, {250, -15}},
  };
  std::vector<uint16_t> pit = {1};
  CHECK(!inPitLane(map, pit, 0, 0), "on the straight");
  CHECK(!inPitLane(map, pit, 0, -6), "nearer the straight than the pit lane");
  CHECK(inPitLane(map, pit, 0, -10), "nearer the pit lane");
  CHECK(inPitLane(map, pit, 100, -15), "in the pit lane");
  CHECK(inPitLane(map, pit, 100, -24), "9 m off the pit lane, on its far side");
  CHECK(!inPitLane(map, pit, 100, -30), "15 m off it: the paddock, not the lane");
  CHECK(!inPitLane(map, {}, 100, -15), "no lines tagged: never");
  CHECK(!inPitLane({}, pit, 100, -15), "no map: never");
  CHECK(!inPitLane(map, {5}, 100, -15), "a tag past the lines does nothing");
  printf("  pit lane: near a tagged line and nearer it than the track\n");
}

static void testClock() {
  CHECK(clockText(0) == "0:00", "zero");
  CHECK(clockText(59999) == "0:59", "under a minute");
  CHECK(clockText(754000) == "12:34", "12:34");
  CHECK(clockText(3600000 + 61000) == "1:01:01", "an hour, got %s", clockText(3661000).c_str());
  CHECK(sessionClock(754000, 0) == "12:34", "no length: time since the start");
  CHECK(sessionClock(0, 20) == "20:00 left", "start of a 20 min session: %s", sessionClock(0, 20).c_str());
  CHECK(sessionClock(1, 20) == "20:00 left", "rounds up");
  CHECK(sessionClock(754000, 20) == "7:26 left", "7:26 left, got %s", sessionClock(754000, 20).c_str());
  CHECK(sessionClock(20 * 60000 - 1, 20) == "0:01 left", "the last millisecond: %s", sessionClock(1199999, 20).c_str());
  CHECK(sessionClock(20 * 60000, 20) == "Time up", "time up");
  CHECK(sessionClock(40 * 60000, 20) == "Time up", "well over");
  CHECK(!sessionEnding(754000, 20) && sessionEnding(19 * 60000 + 1, 20) && sessionEnding(30 * 60000, 20), "ending");
  CHECK(!sessionEnding(30 * 60000, 0), "no length: never ending");
  CHECK(sessionLengthMin(0) == 0 && sessionLengthMin(1) == 20 && sessionLengthMin(3) == 30 && sessionLengthMin(9) == 0,
        "lengths");
  printf("  session clock: time since the start, or a countdown\n");
}

int main() {
  printf("Situation screen tests\n");
  testSlow();
  testPitLane();
  testClock();
  if (failures) {
    printf("\n%d FAILURE(S)\n", failures);
    return 1;
  }
  printf("\nALL PASSED\n");
  return 0;
}
