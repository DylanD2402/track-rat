/**
 * Host-side tests for nudging a timing line (include/LineNudge.h): a straight path at a known
 * speed (moving the finish +0.5 m adds the right milliseconds), a line moved past the end of the
 * slice, headings and lines without one, the standing start (moving the start leaves its time
 * alone), laps (both ends move), and agreement with LapTimer's own crossing times on the same
 * fixes. The Wi-Fi page's JavaScript repeats the numbers (test/test_web.js).
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_linenudge.cpp src/LapTimer.cpp -o /tmp/ln && /tmp/ln
 */

#include <cmath>
#include <cstdio>
#include <vector>
#include "LapTimer.h"
#include "LineNudge.h"
#include "check.h"

using namespace linenudge;

static const double REF_LAT = 45.0, REF_LON = -75.0;
static const int64_t T0 = 36000000;  // 10:00 UTC

struct Pos {
  double x, y;
  float kmh;
};

static TrackPoint pointAt(double x, double y, float heading = -1, float radius = 10) {
  TrackPoint p = {0, 0, radius, heading < 0 ? 0 : heading, heading < 0 ? HeadingFrom::None : HeadingFrom::Set};
  geo::fromLocal(x, y, REF_LAT, REF_LON, p.lat, p.lon);
  return p;
}

// Along a polyline at a steady speed, a fix every 50 ms, starting `lead` metres in
static std::vector<Pos> alongPath(const std::vector<geo::XY>& way, double kmh, double lead) {
  std::vector<Pos> out;
  const double step = kmh / 3.6 * 0.05;
  double total = 0;
  for (size_t i = 1; i < way.size(); i++) total += hypot(way[i].x - way[i - 1].x, way[i].y - way[i - 1].y);
  for (double d = lead; d <= total + 1e-9; d += step) {
    double left = d;
    for (size_t i = 1; i < way.size(); i++) {
      const double len = hypot(way[i].x - way[i - 1].x, way[i].y - way[i - 1].y);
      if (left <= len || i + 1 == way.size()) {
        const double f = left / len;
        out.push_back({way[i - 1].x + f * (way[i].x - way[i - 1].x), way[i - 1].y + f * (way[i].y - way[i - 1].y), (float)kmh});
        break;
      }
      left -= len;
    }
  }
  return out;
}

static std::vector<Fix> toFixes(const std::vector<Pos>& path) {
  std::vector<Fix> out;
  for (size_t i = 0; i < path.size(); i++) {
    double lat, lon;
    geo::fromLocal(path[i].x, path[i].y, REF_LAT, REF_LON, lat, lon);
    Fix f = fixAt(lat, lon, T0 + (int64_t)i * 50, path[i].kmh, REF_LAT, REF_LON);
    f.joined = i > 0;
    out.push_back(f);
  }
  return out;
}

// The same fixes through LapTimer; returns its events' times
struct Result {
  std::vector<uint32_t> laps;  // LapCompleted times
  int started = 0;
};
static Result feed(LapTimer& t, const std::vector<Pos>& path) {
  Result r;
  for (size_t i = 0; i < path.size(); i++) {
    double lat, lon;
    geo::fromLocal(path[i].x, path[i].y, REF_LAT, REF_LON, lat, lon);
    const uint32_t ms = (uint32_t)(T0 + (int64_t)i * 50);
    for (const LapEvent& e : t.onFix(lat, lon, path[i].kmh, ms, ms, -1)) {
      if (e.type == LapEventType::LapCompleted) r.laps.push_back(e.timeMs);
      if (e.type == LapEventType::LapStarted) r.started++;
    }
  }
  return r;
}

static Line lineOf(double x, double y, float heading = -1) {
  Line l;
  l.x = x;
  l.y = y;
  l.heading = heading < 0 ? 0 : heading;
  l.hasHeading = heading >= 0;
  return l;
}

static Line shifted(Line l, double metres) {
  shift(l, metres);
  return l;
}

// Straight east at 90 km/h (1.25 m a fix); start at x = 0, finish at x = 600 (24 s on)
static void testStraightRun() {
  const std::vector<Pos> path = alongPath({{-100.3, 0}, {700, 0}}, 90, 0);
  const std::vector<Fix> all = toFixes(path);
  Line start = lineOf(0, 0, 90), fin = lineOf(600, 0, 90);
  const std::vector<Fix> sa = sliceNear(all, start.x, start.y), sb = sliceNear(all, fin.x, fin.y);
  CHECK(sa.size() < 120 && sb.size() < 120 && sa.size() > 40, "slices are small: %d and %d of %d", (int)sa.size(), (int)sb.size(), (int)all.size());

  const int64_t t0 = crossingTimeMs(sa, start, 10), t1 = crossingTimeMs(sb, fin, 10);
  CHECK(t0 != NONE && t1 != NONE, "both crossed");
  CHECK(llabs(newRunMs(t0, t1) - 24000) <= 1, "24 s run: %lld", (long long)newRunMs(t0, t1));
  CHECK(crossingTimeMs(all, fin, 10) == t1, "a slice finds what the whole log does");

  // The finish 0.5 m later: 0.5 m at 25 m/s is 20 ms
  Line fin2 = fin;
  CHECK(shift(fin2, 0.5), "shifted");
  CHECK(fabs(fin2.x - 600.5) < 1e-9 && fabs(fin2.y) < 1e-9, "east when the heading is east");
  const int64_t t2 = crossingTimeMs(sb, fin2, 10);
  CHECK(llabs(newRunMs(t0, t2) - 24020) <= 1, "finish +0.5 m adds 20 ms: %lld", (long long)newRunMs(t0, t2));
  CHECK(llabs(newRunMs(t0, crossingTimeMs(sb, shifted(fin, -1.5), 10)) - 23940) <= 1, "-1.5 m takes 60 ms off");
  // The start 0.5 m later makes the run 20 ms shorter (a rolling start)
  CHECK(llabs(newRunMs(crossingTimeMs(sa, shifted(start, 0.5), 10), t1) - 23980) <= 1, "start +0.5 m takes 20 ms off");
  // A run under the minimum is none
  CHECK(newRunMs(t0, t1, false, NONE, 30000) == NONE, "shorter than the minimum");
  CHECK(newRunMs(NONE, t1) == NONE && newRunMs(t0, NONE) == NONE, "a missing crossing");

  // Against LapTimer on the same fixes, for several finish positions
  for (double dx : {0.0, 0.5, -2.0, 7.3}) {
    LapTimer timer;
    timer.begin(pointAt(0, 0, 90), {});
    timer.setStage(pointAt(600 + dx, 0, 90), true);
    const Result r = feed(timer, path);
    const int64_t mine = newRunMs(t0, crossingTimeMs(sb, shifted(fin, dx), 10));
    CHECK(r.laps.size() == 1, "LapTimer timed the run (finish %+.1f)", dx);
    CHECK(r.laps.size() == 1 && (int64_t)r.laps[0] == mine, "same as LapTimer for %+.1f m: %lld vs %u", dx, (long long)mine,
          r.laps.empty() ? 0u : r.laps[0]);
  }
}

static void testPastTheEndOfTheSlice() {
  const std::vector<Pos> path = alongPath({{-100.3, 0}, {700, 0}}, 90, 0);
  const std::vector<Fix> all = toFixes(path);
  Line fin = lineOf(600, 0, 90);
  const std::vector<Fix> s = sliceNear(all, fin.x, fin.y, 30);
  const Line far = shifted(fin, 100);  // the slice (30 m) ends before x = 700
  CHECK(crossingTimeMs(s, far, 10) == NONE, "past the end of the slice");
  CHECK(crossingTimeMs(s, shifted(fin, -200), 10) == NONE, "before the start of the slice");
  CHECK(crossingTimeMs(s, shifted(fin, 20), 10) != NONE, "inside it");
  CHECK(newRunMs(0, crossingTimeMs(s, far, 10)) == NONE, "so no run time");

  // Two stretches of the slice that aren't next to each other don't make a crossing
  std::vector<Fix> gap(2);
  gap[0].x = -2;
  gap[1].x = 2;
  gap[0].ms = T0;
  gap[1].ms = T0 + 50;
  gap[0].kmh = gap[1].kmh = 50;
  CHECK(crossingTimeMs(gap, lineOf(0, 0), 10) == NONE, "not joined");
  gap[1].joined = true;
  CHECK(crossingTimeMs(gap, lineOf(0, 0), 10) == T0 + 25, "joined: halfway");
  // A gap in the fixes, as LapTimer: nothing is interpolated across it
  gap[1].ms = T0 + MAX_FIX_GAP_MS + 1;
  CHECK(crossingTimeMs(gap, lineOf(0, 0), 10) == NONE, "dropout");
  // Parked on the line
  gap[1].ms = T0 + 50;
  gap[0].kmh = gap[1].kmh = 1;
  CHECK(crossingTimeMs(gap, lineOf(0, 0), 10) == NONE, "too slow to count");
  // Sliced out of a larger run: only the stretch near, and a fix each side
  std::vector<Fix> big;
  for (int i = 0; i < 200; i++) {
    Fix f;
    f.x = i * 10.0;
    f.ms = T0 + i * 50;
    f.kmh = 50;
    f.joined = true;
    big.push_back(f);
  }
  const std::vector<Fix> two = sliceNear(big, 500, 0, 20);  // x 480..520 plus a fix each side
  CHECK(two.size() == 7 && !two[0].joined && two[1].joined && two[6].joined, "slice of %d, first not joined", (int)two.size());
  // Two separate passes: the second one's first fix doesn't join the first pass's last
  std::vector<Fix> loop;
  for (int i = 0; i < 100; i++) {
    Fix f;
    f.x = i < 50 ? i * 10.0 : (100 - i) * 10.0;
    f.ms = T0 + i * 50;
    f.kmh = 50;
    f.joined = true;
    loop.push_back(f);
  }
  const std::vector<Fix> pass = sliceNear(loop, 0, 0, 15);
  int breaks = 0;
  for (const Fix& f : pass) breaks += !f.joined;
  CHECK(breaks == 2, "stretches are separate: %d breaks", breaks);
}

static void testHeadings() {
  const std::vector<Fix> east = toFixes(alongPath({{-100.3, 0}, {100, 0}}, 90, 0));
  const std::vector<Fix> west = toFixes(alongPath({{100.3, 0}, {-100, 0}}, 90, 0));
  Line eastLine = lineOf(0, 0, 90), noHeading = lineOf(0, 0);
  CHECK(crossingTimeMs(east, eastLine, 10) != NONE, "right way");
  CHECK(crossingTimeMs(west, eastLine, 10) == NONE, "wrong way doesn't count");
  CHECK(crossingTimeMs(west, noHeading, 10) != NONE && crossingTimeMs(east, noHeading, 10) != NONE, "no heading: either way");

  // shift: along the heading, perpendicular to the line
  Line n = lineOf(10, 20, 0);
  shift(n, 2);
  CHECK(fabs(n.x - 10) < 1e-9 && fabs(n.y - 22) < 1e-9, "north");
  Line s = lineOf(0, 0, 180);
  shift(s, 1);
  CHECK(fabs(s.x) < 1e-9 && fabs(s.y + 1) < 1e-9, "south: 1 m on is 1 m south");
  Line d = lineOf(0, 0, 45);
  shift(d, -1);
  CHECK(fabs(d.x + sqrt(0.5)) < 1e-9 && fabs(d.y + sqrt(0.5)) < 1e-9, "earlier on a diagonal moves back");
  CHECK(fabs(alongM(d, 5, 5) - (hypot(5, 5) + 1)) < 1e-9, "metres along the heading");

  // No heading: no "along" without a fallback
  Line free = lineOf(5, 5);
  CHECK(!shift(free, 1) && free.x == 5 && free.y == 5, "no direction, no move");
  const float path = pathHeadingDeg(east, noHeading, 10);
  CHECK(fabs(path - 90) < 0.01, "the path crosses heading east: %.2f", path);
  CHECK(fabs(pathHeadingDeg(west, noHeading, 10) - 270) < 0.01, "or west");
  CHECK(isnan(pathHeadingDeg(east, lineOf(0, 500), 10)), "no crossing, no direction");
  CHECK(shift(noHeading, 0.5, path) && fabs(noHeading.x - 0.5) < 1e-6, "moved along the path");
  CHECK(!noHeading.hasHeading, "and still has none");
  CHECK(crossingTimeMs(west, noHeading, 10) != NONE, "either way still");

  // A stored point (lat/lon) moves the same 0.5 m
  TrackPoint p = pointAt(100, 100, 90);
  CHECK(shiftPoint(p, 0.5), "point shifted");
  geo::XY q = geo::toLocal(p.lat, p.lon, REF_LAT, REF_LON);
  CHECK(fabs(q.x - 100.5) < 1e-4 && fabs(q.y - 100) < 1e-4, "lat/lon moved 0.5 m east: %.5f %.5f", q.x, q.y);
  TrackPoint none = pointAt(100, 100);
  CHECK(!shiftPoint(none, 0.5) && shiftPoint(none, 0.5, 0), "a point with no heading needs a fallback");
  q = geo::toLocal(none.lat, none.lon, REF_LAT, REF_LON);
  CHECK(fabs(q.y - 100.5) < 1e-4, "north by the fallback");
  const Line li = lineAt(pointAt(30, 40, 90), REF_LAT, REF_LON);
  CHECK(fabs(li.x - 30) < 1e-4 && fabs(li.y - 40) < 1e-4 && li.hasHeading && li.heading == 90, "lineAt");
}

// Standing start: the car stands 0.4 m behind the line for 2 s, then accelerates at 4 m/s^2 to 90 km/h
static void testStandingStart() {
  std::vector<Pos> path;
  double x = -0.4, v = 0;
  for (int i = 0; i < 40; i++) path.push_back({x, 0, 0});
  while (x < 450) {
    v = fmin(25.0, v + 4 * 0.05);
    x += v * 0.05;
    path.push_back({x, 0, (float)(v * 3.6)});
  }
  const std::vector<Fix> all = toFixes(path);
  const Line start = lineOf(0, 0, 90), fin = lineOf(300, 0, 90);
  // Where the car moved off: the last fix slower than STAGE_STILL_KMH
  int64_t moveOff = NONE;
  size_t stoodAt = 0;
  for (size_t i = 0; i < all.size() && all[i].kmh < STAGE_GO_KMH; i++) {
    if (all[i].kmh < STAGE_STILL_KMH) moveOff = all[i].ms;
    if (all[i].kmh < STAGE_STOPPED_KMH) stoodAt = i;
  }
  const Fix stood = all[stoodAt];
  const std::vector<Fix> sa = sliceNear(all, start.x, start.y), sb = sliceNear(all, fin.x, fin.y);
  const int64_t tFin = crossingTimeMs(sb, fin, 10);

  for (double dx : {0.0, -0.5, 0.5, 3.0}) {
    const Line s = shifted(start, dx);
    const bool standing = standingStartHolds(stood, s);
    const int64_t mine = newRunMs(crossingTimeMs(sa, s, 10), tFin, standing, moveOff);
    LapTimer timer;
    timer.begin(pointAt(s.x, s.y, 90), {});
    timer.setStage(pointAt(300, 0, 90), true);
    const Result r = feed(timer, path);
    // The numbers test_web.js expects of the page's JavaScript on this path
    const int64_t want = dx > 1 ? 13837 : 15016;
    CHECK(llabs(mine - want) <= 1, "start %+.1f m: %lld, expected %lld", dx, (long long)mine, (long long)want);
    CHECK(r.laps.size() == 1, "LapTimer timed it (start %+.1f)", dx);
    CHECK(r.laps.size() == 1 && (int64_t)r.laps[0] == mine, "start %+.1f m, standing=%d: %lld vs LapTimer %u", dx, standing,
          (long long)mine, r.laps.empty() ? 0u : r.laps[0]);
    if (dx <= 0.5) CHECK(standing, "within a metre of where the car stood: still a standing start (%+.1f)", dx);
  }
  CHECK(!standingStartHolds(stood, shifted(start, 3)), "a line 3 m ahead of the car: it starts at the line");
  CHECK(standingStartHolds(stood, lineOf(0, 0)), "a start with no heading is a standing start");
  CHECK(!standingStartHolds(stood, lineOf(50, 0, 90)), "the car stood more than STAGE_ARM_M away");
  // A moved start leaves a standing start's time alone
  const Line a = shifted(start, -0.5), b = shifted(start, 0.5);
  CHECK(newRunMs(crossingTimeMs(sa, a, 10), tFin, standingStartHolds(stood, a), moveOff) ==
            newRunMs(crossingTimeMs(sa, b, 10), tFin, standingStartHolds(stood, b), moveOff),
        "same time whichever side");
  CHECK(newRunMs(NONE, tFin, true, moveOff) == tFin - moveOff, "no start crossing needed when standing");
}

// Laps: one loop is 1000 m at 90 km/h (40 s); the line is both ends of every lap
static void testLaps() {
  const std::vector<geo::XY> loop = {{-100, 0}, {300, 0}, {300, 100}, {-100, 100}, {-100, 0}};
  std::vector<geo::XY> way = loop;
  for (int i = 1; i < 3; i++)
    for (size_t k = 1; k < loop.size(); k++) way.push_back(loop[k]);
  way.push_back({100, 0});
  const std::vector<Pos> path = alongPath(way, 90, 0.3);
  const std::vector<Fix> all = toFixes(path);
  const Line sf = lineOf(0, 0, 90);
  const std::vector<Fix> slice = sliceNear(all, 0, 0);
  CHECK(slice.size() < all.size() / 4, "the slice keeps the passes near the line: %d of %d", (int)slice.size(), (int)all.size());

  for (double dx : {0.0, 0.5, -1.5}) {
    const Line l = shifted(sf, dx);
    const std::vector<int64_t> c = crossingsMs(slice, l, 10);
    LapTimer timer;
    timer.begin(pointAt(l.x, l.y, 90), {});
    const Result r = feed(timer, path);
    CHECK(c.size() == 4 && r.laps.size() == 3, "4 crossings, 3 laps (%+.1f): %d, %d", dx, (int)c.size(), (int)r.laps.size());
    for (size_t i = 0; i + 1 < c.size() && i < r.laps.size(); i++)
      CHECK((int64_t)r.laps[i] == newRunMs(c[i], c[i + 1]), "lap %d at %+.1f m: %lld vs LapTimer %u", (int)i + 1, dx,
            (long long)newRunMs(c[i], c[i + 1]), r.laps[i]);
  }
  // Both ends move together: the crossings move 20 ms, a steady 40 s lap stays 40 s
  const std::vector<int64_t> c0 = crossingsMs(slice, sf, 10), c1 = crossingsMs(slice, shifted(sf, 0.5), 10);
  CHECK(c0.size() == 4 && c1.size() == 4, "crossings");
  CHECK(llabs((c1[0] - c0[0]) - 20) <= 1 && llabs((c1[1] - c0[1]) - 20) <= 1, "each crossing 20 ms later");
  CHECK(llabs(newRunMs(c0[0], c0[1]) - 40000) <= 1, "a 40 s lap");
  CHECK(llabs(newRunMs(c1[0], c1[1]) - newRunMs(c0[0], c0[1])) <= 1, "unchanged by moving the shared line");
}

int main() {
  testStraightRun();
  testPastTheEndOfTheSlice();
  testHeadings();
  testStandingStart();
  testLaps();
  if (failures) {
    printf("%d FAILED\n", failures);
    return 1;
  }
  printf("All line nudge tests passed\n");
  return 0;
}
