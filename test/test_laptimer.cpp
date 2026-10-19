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
#include "check.h"

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
    for (const LapEvent& ev : lt.onFix(lat, lon, v * 3.6f, t, i * (uint32_t)dtMs)) {
      if (ev.type == LapEventType::LapCompleted) {
        completed++;
        double err = fabs(ev.timeMs - expectedMs);
        CHECK(err <= 2.0, "%.0f Hz lap %d: got %u ms, expected %.1f ms", hz, completed, ev.timeMs, expectedMs);
      }
      if (ev.type == LapEventType::SplitCrossed) {
        splitsSeen++;
        CHECK(fabs(ev.timeMs - expectedMs / 2) <= 2.0, "split: got %u expected %.1f", ev.timeMs, expectedMs / 2);
      }
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
  testSeed(1);
  int events = 0;
  for (int i = 0; i < 6000; i++) {
    double jitterN = ((testRand() % 2001) - 1000) / 1000.0 * 3.0;  // +/- 3 m
    double jitterE = ((testRand() % 2001) - 1000) / 1000.0 * 3.0;
    events += (int)lt.onFix(SF_LAT + jitterN / 111194.93, SF_LON + jitterE / 88800.0, 1.0f, 100 * i, 100 * i).count;
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
    for (const LapEvent& ev : lt.onFix(lat, lon, v * 3.6f, (uint32_t)t, (uint32_t)t)) {
      if (ev.type != LapEventType::LapCompleted) continue;
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
  LapEvent ev = lt.onFix(lat, lon, 100, 3000, 3000).last();
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
    for (const LapEvent& ev : lt.onFix(lat, lon, v * 3.6f, 3600000 + 100 * i, 100 * i)) {
      if (ev.type == LapEventType::LapCompleted) completed++;
      if (ev.type == LapEventType::LapStarted && starts) (*starts)++;
      if (ev.type == LapEventType::SplitCrossed && splits) (*splits)++;
    }
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

// Directions guessed from the map for lines that have none.
static void testGuessHeadings() {
  // Map drawn clockwise: north up the left side through the S/F at (0, 0),
  // south down the right side through a split at (300, 0).
  layout::Lines map = {loop({{0, -100}, {0, 100}, {300, 100}, {300, -100}})};
  std::vector<geo::XY> splitAt = {{300, 0}};
  auto split = [] { return std::vector<TrackPoint>{{SF_LAT, SF_LON, 15}}; };

  {  // no directions: both guessed, consistent with each other
    TrackPoint sf = {SF_LAT, SF_LON, 15};
    auto sp = split();
    layout::guessHeadings(map, {0, 0}, sf, splitAt, sp);
    CHECK(sf.headingFrom == HeadingFrom::Map && fabs(sf.heading) < 0.5f, "S/F guess %.1f", sf.heading);
    CHECK(sp[0].headingFrom == HeadingFrom::Map && fabs(sp[0].heading - 180) < 0.5f, "split guess %.1f",
          sp[0].heading);
  }
  {  // S/F set north, map agrees: split guessed south
    TrackPoint sf = line(SF_LAT, SF_LON, 0, HeadingFrom::Set);
    auto sp = split();
    layout::guessHeadings(map, {0, 0}, sf, splitAt, sp);
    CHECK(sf.headingFrom == HeadingFrom::Set && sf.heading == 0, "set S/F must not change");
    CHECK(fabs(sp[0].heading - 180) < 0.5f, "split with agreeing map %.1f", sp[0].heading);
  }
  {  // S/F set south (the map is drawn backwards): split turned round to north
    TrackPoint sf = line(SF_LAT, SF_LON, 180, HeadingFrom::Set);
    auto sp = split();
    layout::guessHeadings(map, {0, 0}, sf, splitAt, sp);
    CHECK(sp[0].headingFrom == HeadingFrom::Map && (sp[0].heading < 0.5f || sp[0].heading > 359.5f),
          "split with backwards map %.1f", sp[0].heading);
  }
  {  // split on a different map line (may run either way): left alone
    layout::Lines two = {{{0, -100}, {0, 100}}, {{300, -100}, {300, 100}}};
    TrackPoint sf = line(SF_LAT, SF_LON, 0, HeadingFrom::Set);
    auto sp = split();
    layout::guessHeadings(two, {0, 0}, sf, splitAt, sp);
    CHECK(!sp[0].hasHeading(), "split on another line got %.1f", sp[0].heading);
  }
  {  // S/F nowhere near the map: nothing guessed
    TrackPoint sf = {SF_LAT, SF_LON, 15};
    auto sp = split();
    layout::guessHeadings(map, {150, 0}, sf, splitAt, sp);
    CHECK(!sf.hasHeading() && !sp[0].hasHeading(), "far from the map: no guesses");
  }
  {  // a split that already has a direction keeps it; an empty map guesses nothing
    TrackPoint sf = {SF_LAT, SF_LON, 15};
    std::vector<TrackPoint> sp = {line(SF_LAT, SF_LON, 45, HeadingFrom::Set)};
    layout::guessHeadings(map, {0, 0}, sf, splitAt, sp);
    CHECK(sp[0].heading == 45 && sp[0].headingFrom == HeadingFrom::Set, "set split changed");
    TrackPoint sf2 = {SF_LAT, SF_LON, 15};
    auto sp2 = split();
    layout::guessHeadings(layout::Lines(), {0, 0}, sf2, splitAt, sp2);
    CHECK(!sf2.hasHeading(), "empty map");
  }
  printf("  directions guessed from the map: S/F, splits, backwards map, other lines\n");
}

// Odd inputs to the matching helpers.
static void testMatchEdgeCases() {
  layout::Lines map = {loop({{0, -100}, {0, 100}, {300, 100}, {300, -100}})};
  float h;
  CHECK(!layout::headingFromMap(layout::Lines(), 0, 0, 15, h), "empty map");
  CHECK(!layout::headingFromMap({{{5, 5}, {5, 5}}}, 5, 5, 15, h), "zero-length line");
  CHECK(layout::headingFromMap({{{0, 0}, {0, 3}}}, 0, 1, 15, h) && fabs(h) < 0.5f, "short 2-point line %.1f", h);
  CHECK(layout::score({}, map).value() == 0 && layout::score({{0, 0}}, map).value() == 0, "empty path scores 0");
  CHECK(layout::score({{0, 0}, {0, 50}}, layout::Lines()).value() == 0, "empty map scores 0");
  std::vector<geo::XYf> dup = {{0, -100}, {0, -100}, {0, 100}, {0, 100}, {300, 100}, {300, -100}, {0, -100}};
  CHECK(layout::score(dup, map).value() > 0.95f, "repeated points %.2f", layout::score(dup, map).value());

  // within() must agree with the exact nearest distance
  testSeed(7);
  int disagree = 0;
  for (int i = 0; i < 2000; i++) {
    float x = (testRand() % 4000) / 10.0f - 50, y = (testRand() % 3000) / 10.0f - 150;
    bool exact = layout::nearestD2(map, x, y) <= 15.0f * 15.0f;
    disagree += exact != layout::within(map, x, y, 15.0f);
  }
  CHECK(disagree == 0, "within() disagreed %d times", disagree);

  // A guessed (Map) direction can be backwards, so pathCrosses ignores it
  std::vector<geo::XYf> north = {{0, -50}, {0, 50}};
  CHECK(layout::pathCrosses(north, 0, 0, line(0, 0, 180, HeadingFrom::Map)), "Map direction ignored");
  CHECK(!layout::pathCrosses(north, 30, 0, TrackPoint{0, 0, 15}), "30 m off a 15 m gate");

  // resample: spacing, and both ends kept
  std::vector<geo::XYf> pts;
  layout::resample({{0, 0}, {0, 25}, {0, 25}, {0, 47}}, 10, pts);
  CHECK(pts.size() == 6 && fabs(pts[4].y - 40) < 1e-3 && pts.back().y == 47, "resample %zu points", pts.size());

  // No candidates, or a candidate with no map: stay
  CHECK(layout::pickLayout(north, 0, {}) == -1, "no candidates");
  CHECK(layout::pickLayout(north, 0, {{{0, 0}, TrackPoint{0, 0, 15}, nullptr}}) == -1, "candidate without a map");
  printf("  matching helpers with empty, repeated and odd input\n");
}

// Lap timer corner cases for directions and changing layout.
static void testLapTimerDirectionEdges() {
  const double lapS = 2 * M_PI * R / 30.0;
  {  // travelling exactly across the heading (90 degrees off) doesn't count; 89 does; 359 vs north does
    geo::XY a = {0, -5}, b = {0, 5};
    CHECK(!geo::travelsWith(a, b, 90) && !geo::travelsWith(a, b, 270), "perpendicular");
    CHECK(geo::travelsWith(a, b, 89) && geo::travelsWith(a, b, 359) && geo::travelsWith(a, b, -45),
          "within 90 degrees");
  }
  {  // parked on a line with a direction: GPS jitter makes no events
    LapTimer lt;
    lt.begin(line(SF_LAT, SF_LON, 0, HeadingFrom::Map), {});
    testSeed(3);
    int events = 0;
    for (int i = 0; i < 3000; i++) {
      double n = ((testRand() % 2001) - 1000) / 1000.0 * 3.0, e = ((testRand() % 2001) - 1000) / 1000.0 * 3.0;
      events += (int)lt.onFix(SF_LAT + n / 111194.93, SF_LON + e / 88800.0, 1.0f, 100 * i, 100 * i).count;
    }
    CHECK(events == 0 && lt.startFinish().heading == 0, "parked: %d events, heading %.0f", events,
          lt.startFinish().heading);
  }
  {  // a backwards guess that's turned round goes back to the guess on begin() (a new session)
    LapTimer lt;
    TrackPoint guess = line(SF_LAT, SF_LON, 180, HeadingFrom::Map);
    lt.begin(guess, {});
    driveCircle(lt, true, 2.5 * lapS);
    CHECK(fabs(lt.startFinish().heading) < 0.01, "flipped");
    lt.begin(guess, {});
    CHECK(lt.startFinish().heading == 180, "begin() restores the given line");
  }
  {  // a backwards guess also turns round guessed splits, but not set ones
    const double splitLon = SF_LON - 2 * R / (111194.93 * cos(SF_LAT * geo::DEG2RAD));
    LapTimer lt;
    lt.begin(line(SF_LAT, SF_LON, 180, HeadingFrom::Map),
             {line(SF_LAT, splitLon, 0, HeadingFrom::Map), line(SF_LAT, splitLon, 180, HeadingFrom::Set)});
    int splits = 0;
    int laps = driveCircle(lt, true, 4.5 * lapS, nullptr, &splits);
    CHECK(laps == 3 && fabs(lt.splits()[0].heading - 180) < 0.01 && lt.splits()[1].heading == 180,
          "split flip: %d laps, split headings %.0f %.0f", laps, lt.splits()[0].heading, lt.splits()[1].heading);
    bool both = lt.lapCount() == 3;
    for (size_t i = 0; i < lt.lapCount(); i++) both = both && lt.lap(i).splitMs[0] > 0 && lt.lap(i).splitMs[1] > 0;
    CHECK(both, "both splits timed on every lap after the flip");
  }
  {  // changeGates mid-session keeps the laps and resizes the current lap's splits
    LapTimer lt;
    lt.begin(line(SF_LAT, SF_LON, 0, HeadingFrom::Set), {});
    driveCircle(lt, true, 1.5 * lapS);
    CHECK(lt.lapCount() == 1, "1 lap before changeGates");
    const double splitLon = SF_LON - 2 * R / (111194.93 * cos(SF_LAT * geo::DEG2RAD));
    lt.changeGates(line(SF_LAT, SF_LON, 0, HeadingFrom::Set), {line(SF_LAT, splitLon, 180, HeadingFrom::Set),
                                                               line(SF_LAT, splitLon, 180, HeadingFrom::Set)});
    CHECK(lt.lapCount() == 1 && lt.lapRunning(), "laps kept");
  }
  {  // layouts share a start/finish when it's in the same place and not set to run the other way
    const double mLon = 111194.93 * cos(SF_LAT * geo::DEG2RAD);
    TrackPoint north = line(SF_LAT, SF_LON, 0, HeadingFrom::Set);
    CHECK(layout::sharesLine(north, line(SF_LAT, SF_LON + 3 / mLon, 10, HeadingFrom::Set)), "3 m apart, same way");
    CHECK(!layout::sharesLine(north, line(SF_LAT, SF_LON + 6 / mLon, 0, HeadingFrom::Set)), "6 m apart");
    CHECK(!layout::sharesLine(north, line(SF_LAT, SF_LON, 180, HeadingFrom::Set)), "Reverse on the same line");
    CHECK(layout::sharesLine(north, line(SF_LAT, SF_LON, 180, HeadingFrom::Map)), "a guessed direction may be backwards");
    CHECK(layout::sharesLine(TrackPoint{SF_LAT, SF_LON, 15}, line(SF_LAT, SF_LON, 180, HeadingFrom::Set)),
          "a line with no direction");
  }
}

// --- end-to-end: a venue with several layouts, driven by simulated GPS ------

// Position s metres along a polyline (local metres), wrapping round.
static geo::XY posOnLoop(const std::vector<geo::XYf>& pts, double s) {
  double total = 0;
  for (size_t i = 0; i + 1 < pts.size(); i++) total += hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
  s = fmod(fmod(s, total) + total, total);
  for (size_t i = 0; i + 1 < pts.size(); i++) {
    double len = hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
    if (s <= len) return {pts[i].x + (pts[i + 1].x - pts[i].x) * s / len, pts[i].y + (pts[i + 1].y - pts[i].y) * s / len};
    s -= len;
  }
  return {pts[0].x, pts[0].y};
}

static double loopLength(const std::vector<geo::XYf>& pts) {
  double total = 0;
  for (size_t i = 0; i + 1 < pts.size(); i++) total += hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
  return total;
}

struct SimLayout {
  const char* name;
  TrackPoint sf;  // lat/lon line
  std::vector<TrackPoint> splits;
  layout::Lines map;
  geo::XY sfAt;  // local metres from the venue reference (SF_LAT, SF_LON)
};

static TrackPoint lineAt(double x, double y, float heading, HeadingFrom from) {
  double lat, lon;
  geo::fromLocal(x, y, SF_LAT, SF_LON, lat, lon);
  return line(lat, lon, heading, from);
}

// What main.cpp does: time laps with the selected layout; after the first lap
// (or every 2 km before any line is crossed) compare the path with the other
// layouts and switch if one clearly fits. Returns the layout it ends on.
struct SimResult {
  int finalLayout;
  int switches;
  size_t lapsAfter;
  uint32_t lastLapMs;
  bool lapsKept;
};

static SimResult simulateSession(const std::vector<SimLayout>& layouts, int selected,
                                 const std::vector<geo::XYf>& driven, double startS, double seconds,
                                 std::vector<geo::XYf> leadIn = {}, double noiseM = 0) {
  const double v = 20;  // m/s, 10 Hz
  LapTimer lt;
  lt.begin(layouts[selected].sf, layouts[selected].splits);
  bool checked = false, lapsKept = true;
  int switches = 0;
  std::vector<geo::XYf> drivenPath;
  float drivenM = 0, nextCheck = 2000;
  double leadLen = leadIn.size() > 1 ? loopLength(leadIn) : 0;  // an open path onto the driven loop

  auto check = [&](const std::vector<geo::XYf>& pathVenue, bool afterLap) {
    // main works in the selected layout's frame (metres from its S/F)
    geo::XY o = layouts[selected].sfAt;
    std::vector<geo::XYf> path;
    for (const geo::XYf& p : pathVenue) path.push_back({(float)(p.x - o.x), (float)(p.y - o.y)});
    std::vector<layout::Lines> maps;
    std::vector<layout::Candidate> cands;
    std::vector<int> idx;
    for (size_t i = 0; i < layouts.size(); i++) {
      if ((int)i == selected) continue;
      layout::Lines m = layouts[i].map;
      for (auto& l : m)
        for (auto& p : l) p.x -= (float)o.x, p.y -= (float)o.y;
      maps.push_back(m);
      idx.push_back(i);
    }
    for (size_t k = 0; k < idx.size(); k++)
      cands.push_back({{layouts[idx[k]].sfAt.x - o.x, layouts[idx[k]].sfAt.y - o.y}, layouts[idx[k]].sf, &maps[k]});
    layout::Lines curMap = layouts[selected].map;
    for (auto& l : curMap)
      for (auto& p : l) p.x -= (float)o.x, p.y -= (float)o.y;
    float cur = afterLap ? layout::score(path, curMap).value() : 0;
    int k = layout::pickLayout(path, cur, cands);
    if (k < 0) return false;
    int next = idx[k];
    bool sameLine = layout::sharesLine(layouts[selected].sf, layouts[next].sf);
    size_t before = lt.lapCount();
    if (sameLine && lt.lapRunning()) lt.changeGates(layouts[next].sf, layouts[next].splits);
    else lt.begin(layouts[next].sf, layouts[next].splits);
    if (lt.lapCount() != before) lapsKept = false;
    selected = next;
    switches++;
    return true;
  };

  int n = (int)(seconds * 10);
  for (int i = 0; i < n; i++) {
    double s = v * 0.1 * i;
    geo::XY p = s < leadLen ? posOnLoop(leadIn, s) : posOnLoop(driven, startS + s - leadLen);
    p.x += noiseM * ((testRand() % 2001) - 1000) / 1000.0;  // GPS wander
    p.y += noiseM * ((testRand() % 2001) - 1000) / 1000.0;
    double lat, lon;
    geo::fromLocal(p.x, p.y, SF_LAT, SF_LON, lat, lon);
    bool completed = lt.onFix(lat, lon, v * 3.6f, 3600000 + 100 * i, 100 * i).find(LapEventType::LapCompleted);
    if (completed && !checked && lt.lapCount() == 1) {
      checked = true;
      std::vector<geo::XYf> path;  // the lap, every 5 m, back in venue metres
      geo::XY o = layouts[selected].sfAt;
      for (const LapTimer::TracePt& tp : lt.bestPath())
        if (path.empty() || hypot(tp.x + o.x - path.back().x, tp.y + o.y - path.back().y) >= 5)
          path.push_back({(float)(tp.x + o.x), (float)(tp.y + o.y)});
      check(path, true);
    }
    if (!checked && !lt.lapRunning() && lt.lapCount() == 0) {  // main.cpp trackDrivenPath
      if (drivenPath.empty() || hypot(p.x - drivenPath.back().x, p.y - drivenPath.back().y) >= 5) {
        if (!drivenPath.empty()) drivenM += hypot(p.x - drivenPath.back().x, p.y - drivenPath.back().y);
        drivenPath.push_back({(float)p.x, (float)p.y});
        if (drivenM >= nextCheck) {
          nextCheck += 2000;
          if (check(drivenPath, false)) checked = true;
        }
      }
    }
  }
  return {selected, switches, lt.lapCount(), lt.lastLap() ? lt.lastLap()->timeMs : 0, lapsKept};
}

static void testLayoutSimulation() {
  // Full: 300 x 200 m, clockwise, S/F at (0, 0) heading north. Short cuts
  // through at x = 100 and shares the S/F. Reverse is Full driven the other
  // way. The kart track is a separate 150 x 100 m loop 300 m east.
  std::vector<geo::XYf> full = loop({{0, -100}, {0, 100}, {300, 100}, {300, -100}});
  std::vector<geo::XYf> shortL = loop({{0, -100}, {0, 100}, {100, 100}, {100, -100}});
  std::vector<geo::XYf> reverse = loop({{0, -100}, {300, -100}, {300, 100}, {0, 100}});
  std::vector<geo::XYf> kart = loop({{600, -50}, {600, 50}, {750, 50}, {750, -50}});
  std::vector<SimLayout> venue = {
      {"Full", lineAt(0, 0, 0, HeadingFrom::Set), {lineAt(300, 0, 180, HeadingFrom::Set)}, {full}, {0, 0}},
      {"Short", lineAt(0, 0, 0, HeadingFrom::Set), {lineAt(100, 0, 180, HeadingFrom::Set)}, {shortL}, {0, 0}},
      {"Reverse", lineAt(0, 0, 180, HeadingFrom::Set), {lineAt(300, 0, 0, HeadingFrom::Set)}, {full}, {0, 0}},
      {"Kart", lineAt(600, 0, 0, HeadingFrom::Set), {}, {kart}, {600, 0}},
  };
  const double v = 20;

  {  // Full picked, Short driven: switches after lap 1, keeps it (shared line), times Short laps
    SimResult r = simulateSession(venue, 0, shortL, 50, 5.3 * 600 / v);
    double expect = 600 / v * 1000;
    CHECK(r.finalLayout == 1 && r.switches == 1 && r.lapsKept, "short: ended on %s, %d switches, kept %d",
          venue[r.finalLayout].name, r.switches, r.lapsKept);
    CHECK(r.lapsAfter == 5 && fabs(r.lastLapMs - expect) <= 2, "short: %zu laps, last %u ms (expected %.0f)",
          r.lapsAfter, r.lastLapMs, expect);
    printf("  sim: Full picked, Short driven -> %s after lap 1, %zu laps of %u ms, lap 1 kept\n",
           venue[r.finalLayout].name, r.lapsAfter, r.lastLapMs);
  }
  {  // Full picked, Full driven: stays (Short doesn't fit, Reverse is crossed the wrong way)
    SimResult r = simulateSession(venue, 0, full, 50, 4.3 * 1000 / v);
    CHECK(r.finalLayout == 0 && r.switches == 0 && r.lapsAfter == 4, "full: ended on %s, %d switches, %zu laps",
          venue[r.finalLayout].name, r.switches, r.lapsAfter);
    printf("  sim: Full picked, Full driven -> stays on %s, %zu laps\n", venue[r.finalLayout].name, r.lapsAfter);
  }
  {  // Short picked, Full driven: switches to Full
    SimResult r = simulateSession(venue, 1, full, 50, 4.3 * 1000 / v);
    CHECK(r.finalLayout == 0 && r.switches == 1 && fabs(r.lastLapMs - 1000 / v * 1000) <= 2,
          "short->full: ended on %s, last lap %u", venue[r.finalLayout].name, r.lastLapMs);
    printf("  sim: Short picked, Full driven -> %s\n", venue[r.finalLayout].name);
  }
  {  // Full picked, Reverse driven: no Full lap ever counts; after 2 km it finds Reverse and times it
    SimResult r = simulateSession(venue, 0, reverse, 50, 6.3 * 1000 / v);
    CHECK(r.finalLayout == 2 && r.switches == 1 && r.lapsAfter >= 3 && fabs(r.lastLapMs - 1000 / v * 1000) <= 2,
          "reverse: ended on %s, %zu laps, last %u", venue[r.finalLayout].name, r.lapsAfter, r.lastLapMs);
    printf("  sim: Full picked, driven backwards -> %s after 2 km, %zu laps\n", venue[r.finalLayout].name,
           r.lapsAfter);
  }
  {  // Full picked, kart track next door driven (after leaving the paddock): finds Kart
    std::vector<geo::XYf> leadIn = {{450, -150}, {600, -150}, {600, -50}};  // paddock road onto the kart track
    SimResult r = simulateSession(venue, 0, kart, 0, 250 / v + 10 * 500 / v, leadIn);
    CHECK(r.finalLayout == 3 && r.switches == 1 && r.lapsAfter >= 3 && fabs(r.lastLapMs - 500 / v * 1000) <= 2,
          "kart: ended on %s, %zu laps, last %u", venue[r.finalLayout].name, r.lapsAfter, r.lastLapMs);
    printf("  sim: Full picked, kart track next door driven -> %s, %zu laps of %u ms\n", venue[r.finalLayout].name,
           r.lapsAfter, r.lastLapMs);
  }
  {  // failure path: driving a loop no layout fits (a car park) never switches
    std::vector<geo::XYf> carPark = loop({{-400, -400}, {-400, -300}, {-250, -300}, {-250, -400}});
    SimResult r = simulateSession(venue, 0, carPark, 0, 8000 / v);
    CHECK(r.finalLayout == 0 && r.switches == 0 && r.lapsAfter == 0, "car park: switched to %s",
          venue[r.finalLayout].name);
    printf("  sim: driving somewhere else entirely -> stays on %s, no laps\n", venue[r.finalLayout].name);
  }
  {  // with 2 m of GPS wander on every fix the right layout is still found
    testSeed(11);
    SimResult r = simulateSession(venue, 0, shortL, 50, 4.3 * 600 / v, {}, 2.0);
    CHECK(r.finalLayout == 1 && r.lapsKept && r.lapsAfter == 4 && fabs(r.lastLapMs - 600 / v * 1000) < 300,
          "noisy short: ended on %s, %zu laps, last %u", venue[r.finalLayout].name, r.lapsAfter, r.lastLapMs);
    testSeed(12);
    r = simulateSession(venue, 0, full, 50, 3.3 * 1000 / v, {}, 2.0);
    CHECK(r.finalLayout == 0 && r.switches == 0, "noisy full: switched to %s", venue[r.finalLayout].name);
    printf("  sim with 2 m GPS wander: Short found, Full kept\n");
  }
  {  // the right layout but without directions: Reverse can't be told from Full, so it stays
    std::vector<SimLayout> noDir = venue;
    for (SimLayout& l : noDir) l.sf.headingFrom = HeadingFrom::None;
    SimResult r = simulateSession(noDir, 0, full, 50, 3.3 * 1000 / v);
    CHECK(r.finalLayout == 0 && r.switches == 0, "no directions: switched to %s", noDir[r.finalLayout].name);
  }
}

// --- Splits and the start/finish crossed in the same segment ---
// Fixes 21 m apart (700 ms at 30 m/s) on the circle, so a segment spans both sides of the line.
// `s` is metres driven along the circle from the start/finish (negative: before it).

static const double LAP_M = 2 * M_PI * R;
static const double SEG_V = 30.0;  // m/s

static TrackPoint splitAtS(double s) {
  double lat, lon;
  circlePos(s / R, lat, lon);
  return {lat, lon, 15, 0, HeadingFrom::None};
}

static LapEvents fixAtS(LapTimer& lt, double s) {
  double lat, lon;
  circlePos(s / R, lat, lon);
  uint32_t ms = 3600000 + (uint32_t)llround(s / SEG_V * 1000.0 + 1000.0);  // +1 s keeps s = -12 positive
  return lt.onFix(lat, lon, SEG_V * 3.6f, ms, ms);
}

// Drives from s0 in 21 m steps up to s1 and returns the events of each fix that had any.
static std::vector<LapEvents> driveSegments(LapTimer& lt, double s0, double s1) {
  std::vector<LapEvents> out;
  for (double s = s0; s <= s1; s += 21.0) {
    LapEvents e = fixAtS(lt, s);
    if (!e.empty()) out.push_back(e);
  }
  return out;
}

static double expectMs(double s) { return s / SEG_V * 1000.0; }

// A split just before the line, in the same segment as the line: both are reported, the split first.
static void testSplitBeforeLine() {
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15}, {splitAtS(-4)});
  std::vector<LapEvents> evs = driveSegments(lt, -8, LAP_M + 40);
  CHECK(evs.size() == 2, "expected 2 fixes with events, got %zu", evs.size());
  if (evs.size() != 2) return;

  // Lap 1 starts at the line; the split before it isn't a lap-1 split.
  CHECK(evs[0].count == 1 && evs[0].items[0].type == LapEventType::LapStarted, "lap 1 start: %zu events", evs[0].count);

  const LapEvents& e = evs[1];
  CHECK(e.count == 2, "expected split + lap, got %zu events", e.count);
  if (e.count != 2) return;
  CHECK(e.items[0].type == LapEventType::SplitCrossed && e.items[0].splitIndex == 0, "first event %d",
        (int)e.items[0].type);
  CHECK(fabs(e.items[0].timeMs - expectMs(LAP_M - 4)) <= 5, "split time %u, expected %.0f", e.items[0].timeMs,
        expectMs(LAP_M - 4));
  CHECK(e.items[1].type == LapEventType::LapCompleted, "second event %d", (int)e.items[1].type);
  CHECK(fabs(e.items[1].timeMs - expectMs(LAP_M)) <= 5, "lap time %u", e.items[1].timeMs);
  CHECK(lt.lapCount() == 1 && lt.lap(0).splitMs[0] == (int32_t)e.items[0].timeMs, "split stored on the lap");
  printf("  split just before the line, same segment: split %u ms, then lap %u ms\n", e.items[0].timeMs,
         e.items[1].timeMs);
}

// A split just after the line belongs to the new lap, timed from the new lap's start. The lap that ended
// (started with startLapNow, so it never crossed the split) keeps -1; the delta is against that lap.
static void testSplitAfterLine() {
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15}, {splitAtS(4)});
  fixAtS(lt, 98);
  lt.startLapNow(3600000 + (uint32_t)llround(98 / SEG_V * 1000.0 + 1000.0), 0);
  std::vector<LapEvents> evs = driveSegments(lt, 119, 2 * LAP_M + 40);
  CHECK(evs.size() == 2, "expected 2 fixes with events, got %zu", evs.size());
  if (evs.size() != 2) return;

  const LapEvents& e = evs[0];
  CHECK(e.count == 2, "expected lap + split, got %zu events", e.count);
  if (e.count != 2) return;
  CHECK(e.items[0].type == LapEventType::LapCompleted, "first event %d", (int)e.items[0].type);
  CHECK(e.items[1].type == LapEventType::SplitCrossed && e.items[1].splitIndex == 0, "second event %d",
        (int)e.items[1].type);
  CHECK(fabs(e.items[1].timeMs - expectMs(4)) <= 5, "split time %u, expected %.0f", e.items[1].timeMs, expectMs(4));
  CHECK(e.items[1].deltaMs == NO_DELTA, "the lap that ended never crossed the split: delta %d", e.items[1].deltaMs);
  CHECK(lt.lap(0).splitMs[0] == -1, "lap 1 split %d, expected -1", lt.lap(0).splitMs[0]);

  // The next lap end: lap 2 has the split at ~133 ms, and the split after that line is lap 3's.
  const LapEvents& e2 = evs[1];
  CHECK(e2.count == 2 && e2.items[0].type == LapEventType::LapCompleted &&
            e2.items[1].type == LapEventType::SplitCrossed,
        "lap 2 end: %zu events", e2.count);
  CHECK(lt.lapCount() == 2 && fabs(lt.lap(1).splitMs[0] - expectMs(4)) <= 5, "lap 2 split %d",
        lt.lapCount() > 1 ? lt.lap(1).splitMs[0] : -2);
  printf("  split just after the line: lap 2 split %d ms, lap 1 split %d\n", lt.lap(1).splitMs[0],
         lt.lap(0).splitMs[0]);
}

// The first lap starts at the line and a split just after it is in the same segment.
// At the end of lap 1 the split after the line compares with lap 1, which has just been completed.
static void testSplitAfterLapOneStart() {
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15}, {splitAtS(4)});
  std::vector<LapEvents> evs = driveSegments(lt, -8, LAP_M + 40);
  CHECK(evs.size() == 2, "expected 2 fixes with events, got %zu", evs.size());
  if (evs.size() != 2) return;

  const LapEvents& e = evs[0];
  CHECK(e.count == 2 && e.items[0].type == LapEventType::LapStarted && e.items[1].type == LapEventType::SplitCrossed,
        "lap 1 start: %zu events", e.count);
  if (e.count != 2) return;
  CHECK(fabs(e.items[1].timeMs - expectMs(4)) <= 5, "lap 1 split time %u", e.items[1].timeMs);
  CHECK(e.items[1].deltaMs == NO_DELTA, "no best lap yet: delta %d", e.items[1].deltaMs);

  const LapEvents& e2 = evs[1];
  CHECK(e2.count == 2 && e2.items[0].type == LapEventType::LapCompleted &&
            e2.items[1].type == LapEventType::SplitCrossed,
        "lap 1 end: %zu events", e2.count);
  if (e2.count != 2) return;
  CHECK(e2.items[0].isBest, "lap 1 is the best lap");
  CHECK(e2.items[1].deltaMs != NO_DELTA && abs(e2.items[1].deltaMs) <= 5, "delta vs the lap just completed: %d",
        e2.items[1].deltaMs);
  printf("  split just after the lap 1 start / next lap's split vs lap 1: delta %+d ms\n", e2.items[1].deltaMs);
}

// Two splits before the line and one after it in one segment: reported in the order they were crossed,
// not in index order.
static void testSeveralCrossingsOneSegment() {
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15}, {splitAtS(-2), splitAtS(-7), splitAtS(5)});
  std::vector<LapEvents> evs = driveSegments(lt, -12, LAP_M + 40);
  CHECK(evs.size() == 2, "expected 2 fixes with events, got %zu", evs.size());
  if (evs.size() != 2) return;

  CHECK(evs[0].count == 2 && evs[0].items[0].type == LapEventType::LapStarted &&
            evs[0].items[1].type == LapEventType::SplitCrossed && evs[0].items[1].splitIndex == 2,
        "lap 1 start: %zu events", evs[0].count);

  const LapEvents& e = evs[1];
  CHECK(e.count == 4, "expected 4 events, got %zu", e.count);
  if (e.count != 4) return;
  CHECK(e.items[0].type == LapEventType::SplitCrossed && e.items[0].splitIndex == 1, "event 0: split %d",
        e.items[0].splitIndex);
  CHECK(e.items[1].type == LapEventType::SplitCrossed && e.items[1].splitIndex == 0, "event 1: split %d",
        e.items[1].splitIndex);
  CHECK(e.items[2].type == LapEventType::LapCompleted, "event 2 type %d", (int)e.items[2].type);
  CHECK(e.items[3].type == LapEventType::SplitCrossed && e.items[3].splitIndex == 2, "event 3: split %d",
        e.items[3].splitIndex);
  CHECK(e.items[0].timeMs < e.items[1].timeMs && e.items[1].timeMs < e.items[2].timeMs, "times out of order");
  CHECK(fabs(e.items[0].timeMs - expectMs(LAP_M - 7)) <= 5 && fabs(e.items[1].timeMs - expectMs(LAP_M - 2)) <= 5,
        "split times %u, %u", e.items[0].timeMs, e.items[1].timeMs);
  CHECK(fabs(e.items[3].timeMs - expectMs(5)) <= 5, "new lap's split time %u", e.items[3].timeMs);
  printf("  two splits, the line and another split in one segment: %u, %u, lap %u, %u ms\n", e.items[0].timeMs,
         e.items[1].timeMs, e.items[2].timeMs, e.items[3].timeMs);
}

// A split before a line that doesn't count (crossed again too soon) is just a split.
static void testSplitBeforeIgnoredLine() {
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15}, {splitAtS(-4)}, 100000);  // laps under 100 s are "too soon"
  std::vector<LapEvents> evs = driveSegments(lt, -8, LAP_M + 40);
  CHECK(evs.size() == 2, "expected 2 fixes with events, got %zu", evs.size());
  if (evs.size() != 2) return;
  CHECK(evs[1].count == 1 && evs[1].items[0].type == LapEventType::SplitCrossed, "got %zu events", evs[1].count);
  CHECK(lt.lapCount() == 0 && lt.lapRunning() && lt.currentLapNumber() == 1, "the lap carries on");
  printf("  split before a line crossed too soon: just the split\n");
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
  testGuessHeadings();
  testMatchEdgeCases();
  testLapTimerDirectionEdges();
  testLayoutSimulation();
  testSplitBeforeLine();
  testSplitAfterLine();
  testSplitAfterLapOneStart();
  testSeveralCrossingsOneSegment();
  testSplitBeforeIgnoredLine();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
