/**
 * Host-side tests for tracks that learn themselves (TrackLearn.h): a loop
 * driven twice is found, with the line where the first loop began; a drive
 * round the block once, a road driven back the other way, a figure of eight,
 * two different loops through one point, slow passes and loops too long for
 * the window are not. Then the lap timer times both loops from the kept fixes
 * with the line it found.
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_tracklearn.cpp src/LapTimer.cpp -o /tmp/tl && /tmp/tl
 */

#include <cmath>
#include <cstdio>
#include <vector>
#include "LapTimer.h"
#include "TrackLearn.h"
#include "check.h"

using namespace learn;

static const double REF_LAT = 45.30, REF_LON = -76.67;

struct Drive {
  struct Fix {
    double lat, lon;
    float kmh, course;
    uint32_t ms;
  };
  std::vector<Fix> fixes;
  uint32_t ms = 1000;
  double lastX = 0, lastY = 0;
  bool have = false;

  // Drives a path given as metres east/north at a steady speed, a fix every 50 ms (20 Hz)
  template <class Path>
  void drive(Path path, double lengthM, float kmh, double noiseM = 0.0) {
    const double step = kmh / 3.6 * 0.05;
    for (double s = 0; s < lengthM; s += step) {
      double x, y;
      path(s, x, y);
      if (noiseM > 0) {
        x += noiseM * ((testRand() / 32767.0) - 0.5);
        y += noiseM * ((testRand() / 32767.0) - 0.5);
      }
      add(x, y, kmh);
    }
  }
  void add(double x, double y, float kmh) {
    Fix f;
    geo::fromLocal(x, y, REF_LAT, REF_LON, f.lat, f.lon);
    f.kmh = kmh;
    f.course = have ? geo::bearingDeg(x - lastX, y - lastY) : 0;
    f.ms = ms;
    ms += 50;
    lastX = x;
    lastY = y;
    have = true;
    fixes.push_back(f);
  }
};

// An oval of `len` metres starting at (0, 0) heading east, anticlockwise
static void oval(double s, double len, double& x, double& y) {
  const double r = len / (2 * M_PI);
  const double a = fmod(s, len) / r;
  x = r * sin(a);
  y = r - r * cos(a);
}

// Feeds a drive in; the first loop found (or none)
static bool find(const Drive& d, Loop& loop, size_t* atFix = nullptr) {
  LoopFinder f;
  for (size_t i = 0; i < d.fixes.size(); i++) {
    const Drive::Fix& x = d.fixes[i];
    if (f.onFix(x.lat, x.lon, x.kmh, x.course, i > 0, x.ms, &loop)) {
      if (atFix) *atFix = i;
      return true;
    }
  }
  return false;
}

int main() {
  printf("Track learning tests\n");
  testSeed(7);

  // A 1.2 km loop driven three times at 90 km/h, after arriving from the west
  {
    Drive d;
    d.drive([](double s, double& x, double& y) { x = 0; y = s - 400; }, 400, 60);  // arriving from the south
    d.drive([](double s, double& x, double& y) { oval(s, 1200, x, y); }, 3 * 1200 + 100, 90, 1.0);
    Loop loop;
    size_t at = 0;
    CHECK(find(d, loop, &at), "loop driven twice found");
    const geo::XY p = geo::toLocal(loop.lat, loop.lon, REF_LAT, REF_LON);
    CHECK(hypot(p.x, p.y) < 12, "line where the loop began: %.1f, %.1f", p.x, p.y);
    CHECK(headingDiff(loop.headingDeg, 90) < 20, "heading east: %.0f", loop.headingDeg);
    const uint32_t lapMs = (uint32_t)(1200 / 25.0 * 1000);
    CHECK(fabs((double)(loop.t1 - loop.t0) - lapMs) < 1500, "first loop %lu ms", (unsigned long)(loop.t1 - loop.t0));
    CHECK(fabs(loop.lap1M - 1200) < 100 && fabs(loop.lap2M - 1200) < 100, "loop lengths (GPS noise adds a little) %.0f, %.0f", loop.lap1M,
          loop.lap2M);
    CHECK(d.fixes[at].ms > loop.t1 + lapMs - 2000, "found on the third pass, not before");

    // The lap timer times both loops from the kept fixes with that line
    LapTimer timer;
    TrackPoint sf = {loop.lat, loop.lon, (float)DEFAULT_GATE_HALF_WIDTH_M, loop.headingDeg, HeadingFrom::Set};
    timer.begin(sf, {}, MIN_LAP_TIME_MS);
    for (size_t i = 0; i < d.fixes.size(); i++) {
      const Drive::Fix& x = d.fixes[i];
      timer.onFix(x.lat, x.lon, x.kmh, x.ms, x.ms, -1);
    }
    CHECK(timer.lapCount() == 3, "three laps timed from the start of the first loop: %d", (int)timer.lapCount());
    for (size_t i = 0; i < timer.lapCount(); i++)
      CHECK(fabs((double)timer.lap(i).timeMs - lapMs) < 300, "lap %d: %lu ms", (int)i + 1,
            (unsigned long)timer.lap(i).timeMs);
  }

  // Round the block once and home: no track
  {
    Drive d;
    d.drive([](double s, double& x, double& y) { oval(s, 800, x, y); }, 800 + 300, 50);
    d.drive([](double s, double& x, double& y) { x = 300 + s; y = 0; }, 2000, 50);
    Loop loop;
    CHECK(!find(d, loop), "one loop isn't a track");
  }

  // Out along a road and back the same way, over and over: never a track
  {
    Drive d;
    for (int k = 0; k < 3; k++) {
      d.drive([](double s, double& x, double& y) { x = s; y = 0; }, 1000, 80);
      d.drive([](double s, double& x, double& y) { x = 1000 - s; y = 3; }, 1000, 80);
    }
    Loop loop;
    CHECK(!find(d, loop), "there and back isn't a loop");
  }

  // A figure of eight: crossing itself at right angles never matches; the eight driven twice is a track
  {
    const double a = 300;
    std::vector<geo::XY> eight;  // a lemniscate through the origin, by arc length every metre
    double len = 0;
    {
      geo::XY last = {0, 0};
      eight.push_back(last);
      for (int k = 1; k <= 200000; k++) {
        const double t = 2 * M_PI * k / 200000;
        geo::XY p = {a * sin(t), a * sin(t) * cos(t)};
        const double d = hypot(p.x - last.x, p.y - last.y);
        len += d;
        if (len >= eight.size()) eight.push_back(p);
        last = p;
      }
    }
    auto path = [&](double s, double& x, double& y) {
      const geo::XY& p = eight[(size_t)fmod(s, (double)eight.size())];
      x = p.x;
      y = p.y;
    };
    Loop loop;
    Drive once;
    once.drive(path, 2 * len - 20, 70);
    CHECK(!find(once, loop), "an eight driven once and a bit isn't a track (it crosses at right angles)");
    Drive d;
    d.drive(path, 3 * len, 70);
    CHECK(find(d, loop) && fabs(loop.lap1M - len) < 40, "the eight driven twice is a track: %.0f of %.0f m",
          loop.lap1M, len);
  }

  // A short loop, then a long one through the same point: not the same loop twice
  {
    Drive d;
    d.drive([](double s, double& x, double& y) { oval(s, 600, x, y); }, 600, 70);
    d.drive([](double s, double& x, double& y) { oval(s, 2000, x, y); }, 2000 + 50, 70);
    Loop loop;
    CHECK(!find(d, loop), "two different loops");
    // ...but another long one makes the long one a track
    d.drive([](double s, double& x, double& y) { oval(s + 50, 2000, x, y); }, 2000, 70);
    CHECK(find(d, loop) && fabs(loop.lap1M - 2000) < 80, "the long loop twice: %.0f m", loop.lap1M);
  }

  // Too small (under 300 m) or too slow (a car park at 15 km/h): no track
  {
    Drive d;
    d.drive([](double s, double& x, double& y) { oval(s, 250, x, y); }, 6 * 250, 40);
    Loop loop;
    CHECK(!find(d, loop), "a 250 m loop is too small");
    Drive slow;
    slow.drive([](double s, double& x, double& y) { oval(s, 600, x, y); }, 3 * 600 + 50, 15);
    CHECK(!find(slow, loop), "too slow");
  }

  // Loops quick in time but long: two 7 min loops don't fit in the 12 min window
  {
    Drive d;
    const double len = 7 * 60 * 30 / 3.6;  // 7 min at 30 km/h
    d.drive([len](double s, double& x, double& y) { oval(s, len, x, y); }, 3 * len + 50, 30);
    Loop loop;
    CHECK(!find(d, loop), "loops longer than the window allows");
  }

  // A long drive keeps a bounded path
  {
    LoopFinder f;
    Loop loop;
    for (uint32_t i = 0; i < 200000; i++) {
      double lat, lon;
      geo::fromLocal(i * 2.0, 0, REF_LAT, REF_LON, lat, lon);
      f.onFix(lat, lon, 144, 90, true, 1000 + i * 50, &loop);
    }
    CHECK(f.count() <= LoopFinder::MAX_POINTS, "bounded: %zu points", f.count());
    CHECK(f.count() > 1000, "keeps the window: %zu points", f.count());
  }

  // The fix ring keeps the newest, oldest first
  {
    FixRing<int> r;
    r.push(1);  // before begin: nothing kept
    CHECK(r.size() == 0, "no ring yet");
    r.begin(4);
    for (int i = 1; i <= 6; i++) r.push(i);
    CHECK(r.size() == 4 && r[0] == 3 && r[3] == 6, "newest four: %d..%d", r[0], r[3]);
    r.clear();
    r.push(9);
    CHECK(r.size() == 1 && r[0] == 9, "cleared");
    r.release();
    r.push(1);
    CHECK(r.size() == 0, "released");
  }

  CHECK(headingDiff(350, 10) == 20 && headingDiff(10, 190) == 180 && headingDiff(-90, 270) == 0, "heading differences");

  if (failures) {
    printf("%d FAILED\n", failures);
    return 1;
  }
  printf("All track learning tests passed\n");
  return 0;
}
