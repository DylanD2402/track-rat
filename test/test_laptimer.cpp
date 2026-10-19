/**
 * Host-side tests for the lap timing logic (no ESP32 needed).
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_laptimer.cpp src/LapTimer.cpp -o /tmp/lt && /tmp/lt
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "LapTimer.h"
#include "LayoutMatch.h"

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

// --- direction-aware lines --------------------------------------------------

static TrackPoint line(double lat, double lon, float heading, HeadingFrom from) {
  return {lat, lon, 15, heading, from};
}

// Drives `seconds` round the circle at 30 m/s, 10 Hz, forwards (anticlockwise,
// so north at the S/F) or backwards. Returns the number of laps completed.
static int driveCircle(LapTimer& lt, bool forwards, double seconds, int* starts = nullptr, int* splits = nullptr) {
  const double v = 30.0;
  double angle = forwards ? -0.2 : 0.2;
  int completed = 0;
  for (int i = 0; i < (int)(seconds * 10); i++) {
    double lat, lon;
    circlePos(angle, lat, lon);
    LapEvent ev = lt.onFix(lat, lon, v * 3.6f, 3600000 + 100 * i, 100 * i);
    if (ev.type == LapEventType::LapCompleted) completed++;
    if (ev.type == LapEventType::LapStarted && starts) (*starts)++;
    if (ev.type == LapEventType::SplitCrossed && splits) (*splits)++;
    angle += (forwards ? 1 : -1) * v * 0.1 / R;
  }
  return completed;
}

static void testLineDirection() {
  const double lapS = 2 * M_PI * R / 30.0;
  const double splitLon = SF_LON - 2 * R / (111194.93 * cos(SF_LAT * geo::DEG2RAD));

  {  // right way: timed as usual, split too
    LapTimer lt;
    lt.begin(line(SF_LAT, SF_LON, 0, HeadingFrom::Set), {line(SF_LAT, splitLon, 180, HeadingFrom::Set)});
    int splits = 0;
    int laps = driveCircle(lt, true, 4.5 * lapS, nullptr, &splits);
    CHECK(laps == 4, "right way: expected 4 laps, got %d", laps);
    CHECK(splits == 4, "right way: expected 4 splits, got %d", splits);
    if (lt.lapCount()) CHECK(fabs(lt.lap(0).timeMs - lapS * 1000) <= 2, "right way lap %u ms", lt.lap(0).timeMs);
    printf("  line with a direction, driven the right way: %d laps, %d splits\n", laps, splits);
  }
  {  // wrong way: nothing counts
    LapTimer lt;
    lt.begin(line(SF_LAT, SF_LON, 0, HeadingFrom::Set), {line(SF_LAT, splitLon, 180, HeadingFrom::Set)});
    int starts = 0;
    int laps = driveCircle(lt, false, 4.5 * lapS, &starts);
    CHECK(laps == 0 && starts == 0 && !lt.lapRunning(), "wrong way: %d laps, %d starts", laps, starts);
    printf("  line with a direction, driven the wrong way: %d laps, %d starts\n", laps, starts);
  }
  {  // within 90 degrees still counts: a line set at 60 degrees, crossed heading north
    LapTimer lt;
    lt.begin(line(SF_LAT, SF_LON, 60, HeadingFrom::Set), {});
    int laps = driveCircle(lt, true, 2.5 * lapS);
    CHECK(laps == 2, "60 degrees off: expected 2 laps, got %d", laps);
  }
  {  // a split set the wrong way is ignored, the laps still count
    LapTimer lt;
    lt.begin(line(SF_LAT, SF_LON, 0, HeadingFrom::Set), {line(SF_LAT, splitLon, 0, HeadingFrom::Set)});
    int splits = 0;
    int laps = driveCircle(lt, true, 2.5 * lapS, nullptr, &splits);
    CHECK(laps == 2 && splits == 0, "wrong-way split: %d laps, %d splits", laps, splits);
  }
  {  // no direction: either way counts, as before
    for (bool fwd : {true, false}) {
      LapTimer lt;
      lt.begin({SF_LAT, SF_LON, 15}, {{SF_LAT, splitLon, 15}});
      int splits = 0;
      int laps = driveCircle(lt, fwd, 4.5 * lapS, nullptr, &splits);
      CHECK(laps == 4 && splits == 4, "no direction, %s: %d laps, %d splits", fwd ? "forwards" : "backwards", laps,
            splits);
      if (lt.lapCount()) CHECK(fabs(lt.lap(0).timeMs - lapS * 1000) <= 2, "no direction lap %u ms", lt.lap(0).timeMs);
      printf("  line with no direction, driven %s: %d laps, %d splits\n", fwd ? "forwards" : "backwards", laps,
             splits);
    }
  }
}

// Compact kart track: a long thin loop whose return straight runs 10 m from
// the start/finish, inside its 15 m gate. Without a direction, passing on the
// return straight ends the lap halfway round.
static void stadiumPos(double s, double& lat, double& lon) {
  const double L = 400, r = 5, semi = M_PI * r, lap = 2 * L + 2 * semi;
  s = fmod(s, lap);
  double x, y;
  if (s < L) {
    x = 0, y = -200 + s;  // start/finish straight, northbound, S/F at y = 0
  } else if (s < L + semi) {
    double t = (s - L) / r;
    x = r - r * cos(t), y = 200 + r * sin(t);
  } else if (s < 2 * L + semi) {
    x = 2 * r, y = 200 - (s - L - semi);  // return straight, southbound
  } else {
    double t = (s - 2 * L - semi) / r;
    x = r + r * cos(t), y = -200 - r * sin(t);
  }
  lat = SF_LAT + y / 111194.93;
  lon = SF_LON + x / (111194.93 * cos(SF_LAT * geo::DEG2RAD));
}

static void testParallelStraight() {
  const double v = 30, lapM = 800 + 2 * M_PI * 5, lapMs = lapM / v * 1000;
  for (int withHeading = 0; withHeading < 2; withHeading++) {
    LapTimer lt;
    lt.begin(withHeading ? line(SF_LAT, SF_LON, 0, HeadingFrom::Set) : TrackPoint{SF_LAT, SF_LON, 15}, {}, 8000);
    for (int i = 0; i < (int)(4.2 * lapMs / 100); i++) {
      double lat, lon;
      stadiumPos(550 + v * 0.1 * i, lat, lon);  // starts on the return straight
      lt.onFix(lat, lon, v * 3.6f, 100 * i, 100 * i);
    }
    if (withHeading) {
      CHECK(lt.lapCount() == 3, "parallel straight, with direction: expected 3 laps, got %zu", lt.lapCount());
      for (size_t i = 0; i < lt.lapCount(); i++)
        CHECK(fabs(lt.lap(i).timeMs - lapMs) <= 2, "lap %zu: %u ms, expected %.0f", i + 1, lt.lap(i).timeMs, lapMs);
    } else {
      CHECK(lt.lapCount() >= 6 && lt.lap(0).timeMs < lapMs * 0.6, "no direction should give half laps");
    }
    printf("  kart track, return straight 10 m from the line, %s: %zu laps of %u ms (real lap %.0f ms)\n",
           withHeading ? "with direction" : "no direction", lt.lapCount(), lt.lapCount() ? lt.lap(0).timeMs : 0,
           lapMs);
  }
}

// A direction guessed from the map backwards is turned round after crossing
// the line twice the "wrong" way; a direction that was set never is.
static void testGuessedDirection() {
  const double lapS = 2 * M_PI * R / 30.0;
  {
    LapTimer lt;
    lt.begin(line(SF_LAT, SF_LON, 180, HeadingFrom::Map), {});
    int laps = driveCircle(lt, true, 4.5 * lapS);  // 5 crossings: 1 lost, 1 starts timing, 3 laps
    CHECK(laps == 3, "backwards guess: expected 3 laps, got %d", laps);
    CHECK(fabs(lt.startFinish().heading) < 0.01, "heading turned round to %.1f", lt.startFinish().heading);
    printf("  direction guessed backwards from the map: turned round, %d laps from 5 crossings\n", laps);
  }
  {
    LapTimer lt;
    lt.begin(line(SF_LAT, SF_LON, 180, HeadingFrom::Set), {});
    int laps = driveCircle(lt, true, 4.5 * lapS);
    CHECK(laps == 0, "set direction must not turn round: got %d laps", laps);
  }
  {  // a right guess on the kart track: the return straight never turns it round
    const double v = 30, lapMs = (800 + 2 * M_PI * 5) / v * 1000;
    LapTimer lt;
    lt.begin(line(SF_LAT, SF_LON, 0, HeadingFrom::Map), {}, 8000);
    for (int i = 0; i < (int)(3.2 * lapMs / 100); i++) {
      double lat, lon;
      stadiumPos(550 + v * 0.1 * i, lat, lon);
      lt.onFix(lat, lon, v * 3.6f, 100 * i, 100 * i);
    }
    CHECK(lt.lapCount() == 2 && fabs(lt.lap(0).timeMs - lapMs) <= 2, "right guess on kart track: %zu laps",
          lt.lapCount());
    CHECK(fabs(lt.startFinish().heading) < 0.01, "right guess changed to %.1f", lt.startFinish().heading);
  }
}

// --- layouts -----------------------------------------------------------------

static std::vector<geo::XYf> loop(std::vector<geo::XYf> corners) {
  corners.push_back(corners[0]);
  return corners;
}

static void testLayoutMatch() {
  // Full: 300 x 200 m rectangle, clockwise from the S/F at (0, 0) heading north.
  // Short: the same left side, cut through at x = 100.
  layout::Lines full = {loop({{0, -100}, {0, 100}, {300, 100}, {300, -100}})};
  layout::Lines shortMap = {loop({{0, -100}, {0, 100}, {100, 100}, {100, -100}})};
  std::vector<geo::XYf> drivenShort = loop({{0, 0}, {0, 100}, {100, 100}, {100, -100}, {0, -100}});
  std::vector<geo::XYf> drivenFull = loop({{0, 0}, {0, 100}, {300, 100}, {300, -100}, {0, -100}});

  float sShort = layout::score(drivenShort, shortMap).value(), sFull = layout::score(drivenShort, full).value();
  CHECK(sShort > 0.9f && sShort > sFull + layout::MATCH_MARGIN, "short lap: short %.2f full %.2f", sShort, sFull);
  printf("  short layout driven: short map %.2f, full map %.2f\n", sShort, sFull);
  sShort = layout::score(drivenFull, shortMap).value(), sFull = layout::score(drivenFull, full).value();
  CHECK(sFull > 0.9f && sFull > sShort + layout::MATCH_MARGIN, "full lap: short %.2f full %.2f", sShort, sFull);
  printf("  full layout driven: short map %.2f, full map %.2f\n", sShort, sFull);

  // Direction from the map at the S/F: the map runs north there
  float h = -1;
  CHECK(layout::headingFromMap(full, 2, 0, 15, h) && fabs(h) < 0.5f, "map heading %.1f", h);
  CHECK(!layout::headingFromMap(full, 150, 0, 15, h), "no map line near the middle");
  layout::Lines circle(1);  // dense map (a point every ~2.6 m), anticlockwise: north at the S/F
  for (int i = 0; i <= 360; i++)
    circle[0].push_back({(float)(R * (cos(i * M_PI / 180) - 1)), (float)(R * sin(i * M_PI / 180))});
  CHECK(layout::headingFromMap(circle, 0, 0, 15, h) && (h < 3 || h > 357), "circle map heading %.1f", h);

  // A layout's line counts only if the lap crossed it the right way
  TrackPoint north = line(0, 0, 0, HeadingFrom::Set), south = line(0, 0, 180, HeadingFrom::Set);
  CHECK(layout::pathCrosses(drivenFull, 0, 0, north), "crosses north");
  CHECK(!layout::pathCrosses(drivenFull, 0, 0, south), "doesn't cross south (reverse layout)");
  CHECK(layout::pathCrosses(drivenFull, 0, 0, TrackPoint{0, 0, 15}), "no direction: crosses");
  CHECK(!layout::pathCrosses(drivenShort, 300, 0, north), "short lap never reaches x = 300");
  CHECK(layout::pathCrosses(drivenFull, 300, 0, south), "full lap crosses x = 300 heading south");
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
  testLineDirection();
  testParallelStraight();
  testGuessedDirection();
  testLayoutMatch();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
