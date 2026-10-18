/**
 * Host-side tests for the lap timing logic (no ESP32 needed).
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_laptimer.cpp src/LapTimer.cpp -o /tmp/lt && /tmp/lt
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "LapTimer.h"

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

static const double SF_LAT = 37.0, SF_LON = -122.0, R = 150.0;

// Position on a circle of radius R whose easternmost point (angle 0) is the S/F.
static void circlePos(double angle, double& lat, double& lon) {
  lat = SF_LAT + R * sin(angle) / 111194.93;  // metres per degree on a 6371 km sphere
  lon = SF_LON + R * (cos(angle) - 1.0) / (111194.93 * cos(SF_LAT * geo::DEG2RAD));
}

// Constant speed: every lap should be exactly 2*pi*R / v.
static void testConstantSpeed(double hz, uint32_t startTimeMs) {
  LapTimer lt;
  std::vector<TrackPoint> splits = {{SF_LAT, SF_LON - 2 * R / (111194.93 * cos(SF_LAT * geo::DEG2RAD)), 15}};
  lt.begin({SF_LAT, SF_LON, 15}, splits);

  const double v = 30.0;  // m/s
  const double expectedMs = 2 * M_PI * R / v * 1000.0;
  const double dtMs = 1000.0 / hz;
  double angle = -0.2;
  int completed = 0, splitsSeen = 0;

  for (int i = 0; i < (int)(5 * expectedMs / dtMs); i++) {
    double lat, lon;
    circlePos(angle, lat, lon);
    uint32_t t = (uint32_t)llround(startTimeMs + i * dtMs) % 86400000u;
    LapEvent ev = lt.onFix(lat, lon, v * 3.6f, t, i * (uint32_t)dtMs);
    if (ev.type == LapEventType::LapCompleted) {
      completed++;
      double err = fabs(ev.timeMs - expectedMs);
      CHECK(err <= 2.0, "%.0f Hz lap %d: got %u ms, expected %.1f ms", hz, completed, ev.timeMs, expectedMs);
    }
    if (ev.type == LapEventType::SplitCrossed) {
      splitsSeen++;
      CHECK(fabs(ev.timeMs - expectedMs / 2) <= 2.0, "split: got %u expected %.1f", ev.timeMs, expectedMs / 2);
    }
    angle += v * (dtMs / 1000.0) / R;
  }
  CHECK(completed == 4, "%.0f Hz: expected 4 laps, got %d", hz, completed);
  // The drive ends just before the 5th S/F crossing, so lap 5's split is also passed.
  CHECK(splitsSeen == 5, "%.0f Hz: expected 5 splits, got %d", hz, splitsSeen);
  CHECK(lt.lapCount() == 4, "lapCount %zu", lt.lapCount());
  if (lt.lapCount() == 4) {
    CHECK(lt.lap(0).splitMs.size() == 1 && lt.lap(1).splitMs[0] > 0, "split stored on lap");
  }
  printf("  constant speed @ %2.0f Hz from t=%u: %d laps, expected %.1f ms each\n", hz, startTimeMs, completed,
         expectedMs);
}

// Car parked on the line with GPS wander: must not produce laps.
static void testParkedJitter() {
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15}, {});
  srand(1);
  int events = 0;
  for (int i = 0; i < 6000; i++) {
    double jitterN = ((rand() % 2001) - 1000) / 1000.0 * 3.0;  // +/- 3 m
    double jitterE = ((rand() % 2001) - 1000) / 1000.0 * 3.0;
    LapEvent ev = lt.onFix(SF_LAT + jitterN / 111194.93, SF_LON + jitterE / 88800.0, 1.0f, 100 * i, 100 * i);
    if (ev.type != LapEventType::None) events++;
  }
  CHECK(events == 0, "parked jitter produced %d events", events);
  printf("  parked on the line with GPS jitter: %d events\n", events);
}

// Faster second lap becomes best and reports a negative delta.
static void testBestLapAndDelta() {
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15}, {});
  double angle = -0.2, t = 0;
  int lap = 0;
  LapEvent last;
  while (lt.lapCount() < 3) {
    double v = (lap == 1) ? 35.0 : 30.0;
    double lat, lon;
    circlePos(angle, lat, lon);
    LapEvent ev = lt.onFix(lat, lon, v * 3.6f, (uint32_t)t, (uint32_t)t);
    if (ev.type == LapEventType::LapCompleted) {
      last = ev;
      lap++;
      if (lap == 2) CHECK(ev.isBest && ev.deltaMs < -3000, "lap 2 should be best, delta %d", ev.deltaMs);
    }
    angle += v * 0.1 / R;
    t += 100;
  }
  CHECK(lt.bestLapIndex() == 1, "best index %d", lt.bestLapIndex());
  CHECK(!last.isBest && last.deltaMs > 3000, "lap 3 delta %d", last.deltaMs);
  printf("  best lap / delta: best=lap %d, lap 3 delta %+d ms\n", lt.bestLapIndex() + 1, last.deltaMs);
}

// A GPS dropout across the line must not produce a bogus interpolated time.
static void testDropoutAcrossLine() {
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15}, {});
  double lat, lon;
  circlePos(-0.3, lat, lon);
  lt.onFix(lat, lon, 100, 0, 0);
  circlePos(0.3, lat, lon);  // 3 s later, other side of the line
  LapEvent ev = lt.onFix(lat, lon, 100, 3000, 3000);
  CHECK(ev.type == LapEventType::None, "crossing during a 3 s gap should be ignored");
  printf("  crossing during GPS dropout ignored: %s\n", ev.type == LapEventType::None ? "yes" : "no");
}

// Lap 1 at 30 m/s, lap 2 at 25 m/s: the live diff against lap 1 grows
// linearly and is known exactly at any point of lap 2.
static void testLiveDelta() {
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15}, {});
  double angle = -0.2, t = 0;
  bool sawNoDeltaOnLap1 = false;
  int checked = 0;
  while (lt.lapCount() < 2) {
    double v = lt.lapCount() == 0 ? 30.0 : 25.0;
    double lat, lon;
    circlePos(angle, lat, lon);
    lt.onFix(lat, lon, v * 3.6f, (uint32_t)t, (uint32_t)t);
    if (lt.lapRunning() && lt.lapCount() == 0 && lt.liveDeltaMs() == NO_DELTA) sawNoDeltaOnLap1 = true;
    if (lt.lapCount() == 1) {
      double d = angle - 2 * M_PI;  // radians into lap 2
      for (double target : {M_PI / 2, M_PI, 1.5 * M_PI}) {
        if (fabs(d - target) < 0.5 * 25 * 0.1 / R) {
          double expected = (target * R / 25.0 - target * R / 30.0) * 1000.0;
          int32_t got = lt.liveDeltaMs();
          CHECK(got != NO_DELTA && fabs(got - expected) <= 60, "live delta at %.2f rad: got %d expected %.0f",
                target, got, expected);
          printf("  live diff %.0f%% into lap 2: %+d ms (expected %+.0f)\n", target / (2 * M_PI) * 100, got,
                 expected);
          checked++;
        }
      }
    }
    angle += v * 0.1 / R;
    t += 100;
  }
  CHECK(sawNoDeltaOnLap1, "no live diff should be shown before a best lap exists");
  CHECK(checked == 3, "checked %d points", checked);
}

// Bench Test circle: 60 m radius at 25 m/s = 15.1 s laps, which the normal
// 20 s minimum would reject but the 8 s bench minimum accepts.
static void testShortBenchLaps() {
  const double r = 60.0, v = 25.0;
  for (uint32_t minLap : {(uint32_t)MIN_LAP_TIME_MS, (uint32_t)8000}) {
    LapTimer lt;
    lt.begin({SF_LAT, SF_LON, 15}, {}, minLap);
    double angle = -0.3;
    for (int i = 0; i < 700; i++) {  // 70 s
      double lat = SF_LAT + r * sin(angle) / 111194.93;
      double lon = SF_LON + r * (cos(angle) - 1.0) / (111194.93 * cos(SF_LAT * geo::DEG2RAD));
      lt.onFix(lat, lon, v * 3.6f, 100 * i, 100 * i);
      angle += v * 0.1 / r;
    }
    double expected = 2 * M_PI * r / v * 1000.0;
    if (minLap == 8000) {
      CHECK(lt.lapCount() >= 3, "bench: expected 3+ laps, got %zu", lt.lapCount());
      if (lt.lapCount()) CHECK(fabs(lt.lap(0).timeMs - expected) <= 2, "bench lap %u vs %.0f", lt.lap(0).timeMs, expected);
      printf("  bench laps (60 m circle): %zu laps of %u ms (expected %.0f)\n", lt.lapCount(),
             lt.lapCount() ? lt.lap(0).timeMs : 0, expected);
    } else {
      // The 20 s minimum skips every other crossing, giving double-length laps.
      CHECK(lt.lapCount() >= 1 && fabs(lt.lap(0).timeMs - 2 * expected) <= 2,
            "20 s minimum: expected %.0f ms laps", 2 * expected);
    }
  }
}

int main() {
  printf("LapTimer tests\n");
  testConstantSpeed(10, 3600000);
  testConstantSpeed(1, 3600000);
  testConstantSpeed(10, 86400000 - 60000);  // laps run across UTC midnight
  testParkedJitter();
  testBestLapAndDelta();
  testDropoutAcrossLine();
  testLiveDelta();
  testShortBenchLaps();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
