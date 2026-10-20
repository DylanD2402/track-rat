/**
 * Host-side tests for automatic corner sectors (no ESP32 needed): a
 * synthetic track with known corners, driven with a realistic speed
 * profile (full throttle, braking for each corner, corner speed from its
 * radius), cut into sectors and then timed through the real LapTimer.
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_sectors.cpp src/LapTimer.cpp src/VboFormat.cpp -o /tmp/sec && /tmp/sec
 */

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "LapTimer.h"
#include "Sectors.h"
#include "VboFormat.h"
#include "check.h"

using sectors::PathPt;

static const double SF_LAT = 45.0, SF_LON = -76.0;

// A closed track made of straights and left-hand arcs, sampled every 0.5 m.
struct Track {
  struct Piece {
    double length;  // metres
    double radius;  // 0 = straight
  };
  std::vector<Piece> pieces;
  std::vector<double> x, y, limit;  // position and speed limit (m/s) every 0.5 m
  std::vector<double> cornerStart;  // distance where each corner (arc) begins

  void build(double startX, double startY, double heading) {
    const double ds = 0.5, latG = 1.1 * 9.81;
    double px = startX, py = startY, h = heading, dist = 0;  // h: radians from north, clockwise
    for (const Piece& p : pieces) {
      if (p.radius > 0) cornerStart.push_back(dist);
      for (double s = 0; s < p.length; s += ds, dist += ds) {
        x.push_back(px);
        y.push_back(py);
        limit.push_back(p.radius > 0 ? sqrt(latG * p.radius) : 120.0);  // straights: only acceleration limits
        px += sin(h) * ds;
        py += cos(h) * ds;
        if (p.radius > 0) h -= ds / p.radius;  // turning left
      }
    }
  }
  double length() const { return x.size() * 0.5; }
};

// Speed along the track: as fast as the limits allow, accelerating at
// 4 m/s2 and braking at 10 m/s2, round and round (two passes settle the start).
static std::vector<double> speedProfile(const Track& t) {
  const size_t n = t.limit.size();
  const double ds = 0.5, acc = 4.0, brake = 10.0;
  std::vector<double> v(t.limit);
  for (int pass = 0; pass < 2; pass++) {
    for (size_t k = 1; k <= n; k++) {
      size_t i = k % n, j = k - 1;
      v[i] = std::min(v[i], sqrt(v[j] * v[j] + 2 * acc * (1 - v[j] / 75.0) * ds));  // drag: tops out near 270 km/h
    }
    for (size_t k = n; k >= 1; k--) {
      size_t i = k - 1, j = k % n;
      v[i] = std::min(v[i], sqrt(v[j] * v[j] + 2 * brake * ds));
    }
  }
  return v;
}

// One lap at 20 Hz from the start (distance 0), as LapTimer's path holds it.
static std::vector<PathPt> lapAt20Hz(const Track& t, const std::vector<double>& v) {
  std::vector<PathPt> out;
  double time = 0, nextSample = 0;
  for (size_t i = 0; i < t.x.size(); i++) {
    if (time >= nextSample) {
      out.push_back({(float)(t.x[i] - t.x[0]), (float)(t.y[i] - t.y[0]), (int32_t)lround(time * 1000), (float)(v[i] * 3.6)});
      nextSample += 0.05;
    }
    time += 0.5 / v[i];
  }
  out.push_back({0, 0, (int32_t)lround(time * 1000), (float)(v[0] * 3.6)});  // back at the line
  return out;
}

// The track built as a loop: a rectangle-ish course whose pieces add up to 360 degrees.
static Track loopCourse() {
  Track t;
  t.pieces = {
      {400, 0},                     // main straight
      {M_PI / 2 * 20, 20},          // T1: tight 90
      {300, 0},
      {M_PI / 4 * 1000, 1000},      // sweeper, 45 degrees, flat out
      {250, 0},
      {M_PI / 4 * 15, 15},          // T2: slow 45
      {150, 0},
      {M_PI / 2 * 40, 40},          // T3: 90
      {120, 0},                     // a short straight: T4 follows within 3 s...
      {M_PI / 2 * 25, 25},          // T4: 90
  };
  // Close the loop: stretch the eastward straight until the end is due south of the start,
  // then a straight north back to the line
  t.build(0, 0, 0);
  t.pieces[8].length -= t.x.back() - t.x[0];
  Track u;
  u.pieces = t.pieces;
  u.build(0, 0, 0);
  u.pieces.push_back({u.y[0] - u.y.back(), 0});
  Track done;
  done.pieces = u.pieces;
  done.build(0, 0, 0);
  return done;
}

static void testCutsOnSyntheticTrack() {
  Track t = loopCourse();
  std::vector<double> v = speedProfile(t);
  std::vector<PathPt> lap = lapAt20Hz(t, v);
  std::vector<sectors::Cut> cuts = sectors::findCuts(lap);
  printf("  synthetic course: %.0f m, lap %.1f s, %zu cuts\n", t.length(), lap.back().t / 1000.0, cuts.size());
  for (size_t i = 0; i < cuts.size(); i++)
    printf("    T%zu ends at %.1f s, %.0f km/h, heading %.0f\n", i + 1, cuts[i].t / 1000.0, cuts[i].speedKmh,
           cuts[i].headingDeg);

  // Every braking corner gets a cut just before it; the sweeper doesn't
  const int braking[] = {0, 2, 3};  // corners (in cornerStart order) with braking zones far enough apart
  CHECK(cuts.size() == 4, "4 cuts (T1 brakes, T2, T3, T4), got %zu", cuts.size());
  for (size_t i = 0; i < cuts.size() && i < 4; i++) {
    // The corner after this cut: the first corner start beyond it, skipping the flat-out sweeper (index 1)
    double cutDist = 0;
    for (size_t k = 0; k < t.x.size(); k++)
      if (hypot(t.x[k] - t.x[0] - cuts[i].x, t.y[k] - t.y[0] - cuts[i].y) < 0.6) {
        cutDist = k * 0.5;
        break;
      }
    double nextCorner = -1;
    for (size_t c = 0; c < t.cornerStart.size(); c++)
      if (c != 1 && t.cornerStart[c] > cutDist) {
        nextCorner = t.cornerStart[c];
        break;
      }
    // ...within the braking distance from top speed (about 200 m at 10 m/s2)
    CHECK(nextCorner > 0 && nextCorner - cutDist < 260 && nextCorner - cutDist > 10,
          "T%zu's cut %.0f m before its corner", i + 1, nextCorner - cutDist);
  }
  (void)braking;
  // Headings: the first cut is on the main straight, heading north
  if (!cuts.empty()) CHECK(cuts[0].headingDeg < 2 || cuts[0].headingDeg > 358, "T1 cut heads north: %.1f", cuts[0].headingDeg);
  for (size_t i = 1; i < cuts.size(); i++) CHECK(cuts[i].t - cuts[i - 1].t >= SECTOR_MIN_MS, "pieces at least 3 s");
  CHECK(cuts.empty() || cuts[0].t >= SECTOR_MIN_MS, "first piece at least 3 s");
  CHECK(cuts.empty() || lap.back().t - cuts.back().t >= SECTOR_MIN_MS, "last piece at least 3 s");
}

// Hand-made speed traces for the merging rules.
static std::vector<PathPt> trace(const std::vector<std::pair<int32_t, float>>& knots) {
  std::vector<PathPt> out;
  float x = 0;
  for (size_t k = 0; k + 1 < knots.size(); k++)
    for (int32_t t = knots[k].first; t < knots[k + 1].first; t += 50) {
      float f = (float)(t - knots[k].first) / (knots[k + 1].first - knots[k].first);
      float v = knots[k].second + f * (knots[k + 1].second - knots[k].second);
      out.push_back({x, 0, t, v});
      x += v / 3.6f * 0.05f;
    }
  out.push_back({x, 0, knots.back().first, knots.back().second});
  return out;
}

static void testMerging() {
  // Two braking zones 2 s apart (a chicane): one cut, at the faster peak
  std::vector<PathPt> chicane = trace({{0, 150}, {8000, 200}, {10000, 90}, {10600, 130}, {11400, 80},
                                       {20000, 180}, {22000, 70}, {30000, 150}});
  std::vector<sectors::Cut> c = sectors::findCuts(chicane);
  CHECK(c.size() == 2, "chicane merged: 2 cuts, got %zu", c.size());
  if (c.size() == 2) {
    CHECK(c[0].t > 7000 && c[0].t < 9000, "first cut at the 200 km/h peak: %d", c[0].t);
    CHECK(c[1].t > 19000 && c[1].t < 21000, "second at the 180 km/h peak: %d", c[1].t);
  }

  // A lift of 6 km/h isn't a braking zone
  std::vector<PathPt> lift = trace({{0, 150}, {5000, 200}, {6000, 194}, {9000, 210}, {11000, 80}, {20000, 150}});
  c = sectors::findCuts(lift);
  CHECK(c.size() == 1 && c[0].t > 8500, "a lift doesn't cut: %zu cuts", c.size());

  // Braking just after the line, or just before it: those pieces join the next / previous sector
  std::vector<PathPt> nearLine = trace({{0, 180}, {1000, 190}, {2500, 90}, {12000, 200}, {14000, 80},
                                        {28000, 200}, {29000, 205}, {30000, 120}});
  c = sectors::findCuts(nearLine);
  CHECK(c.size() == 1 && c[0].t > 11000 && c[0].t < 13000, "near the line: only the middle cut (%zu)", c.size());

  // No braking at all (a flat-out oval): no sectors
  CHECK(sectors::findCuts(trace({{0, 200}, {30000, 205}})).empty(), "an oval: no cuts");
  CHECK(sectors::findCuts(std::vector<PathPt>()).empty(), "an empty lap: no cuts");
  printf("  merging: a chicane joins, a lift doesn't cut, nothing within 3 s of the line\n");
}

// The cuts as gates, through the real LapTimer: sector times on every lap after.
static void testTiming() {
  Track t = loopCourse();
  std::vector<double> v = speedProfile(t);
  std::vector<PathPt> lap = lapAt20Hz(t, v);
  std::vector<sectors::Cut> cuts = sectors::findCuts(lap);

  std::vector<TrackPoint> gates;
  for (const sectors::Cut& c : cuts) {
    TrackPoint g = {0, 0, 15, c.headingDeg, HeadingFrom::Set};
    geo::fromLocal(c.x, c.y, SF_LAT, SF_LON, g.lat, g.lon);
    gates.push_back(g);
  }
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15, 0, HeadingFrom::Set}, {});
  uint32_t clock = 9 * 3600000u;
  auto drive = [&](int laps) {
    double time = 0, next = 0.025;  // (no fix exactly on the line)
    for (int n = 0; n < laps; n++)
      for (size_t i = 0; i < t.x.size(); i++) {
        if (time >= next) {
          double lat, lon;
          geo::fromLocal(t.x[i] - t.x[0], t.y[i] - t.y[0], SF_LAT, SF_LON, lat, lon);
          lt.onFix(lat, lon, (float)(v[i] * 3.6), clock + (uint32_t)lround(time * 1000), 0);
          next += 0.05;
        }
        time += 0.5 / v[i];
      }
    clock += (uint32_t)lround(time * 1000);
  };
  drive(3);  // the line is crossed between loops: lap 1 starts after the first, ends after the second
  CHECK(lt.lapCount() == 1, "lap 1 done");
  lt.setSectors(gates);
  int sectorEvents = 0;
  double time = 0, next = 0.025;
  for (size_t i = 0; i < t.x.size(); i++) {
    if (time >= next) {
      double lat, lon;
      geo::fromLocal(t.x[i] - t.x[0], t.y[i] - t.y[0], SF_LAT, SF_LON, lat, lon);
      for (const LapEvent& e : lt.onFix(lat, lon, (float)(v[i] * 3.6), clock + (uint32_t)lround(time * 1000), 0))
        sectorEvents += e.type == LapEventType::SectorCrossed;
      next += 0.05;
    }
    time += 0.5 / v[i];
  }
  clock += (uint32_t)lround(time * 1000);
  drive(1);  // crosses the line: lap 3, the first through the sectors, done
  CHECK(lt.lapCount() >= 3, "lap 3 done (%zu)", lt.lapCount());
  CHECK(sectorEvents >= (int)gates.size() - 1, "sector events: %d of %zu", sectorEvents, gates.size());
  if (lt.lapCount() >= 3) {
    const Lap& l = lt.lap(2);
    CHECK(l.sectorMs.size() == gates.size(), "lap 3 has %zu sector times", l.sectorMs.size());
    for (size_t i = 0; i < l.sectorMs.size() && i < cuts.size(); i++)
      CHECK(std::abs(l.sectorMs[i] - cuts[i].t) <= 60, "T%zu ends at %d ms, the cut was at %d", i + 1, l.sectorMs[i],
            cuts[i].t);
    CHECK(lt.lap(0).sectorMs.empty(), "lap 1 had no sectors yet");
    CHECK(l.splitMs.empty(), "splits stay separate");
  }

  // Into the VBO header as splits named T1...
  vbo::Header h;
  h.hasStart = true;
  for (const TrackPoint& g : gates) {
    vbo::Gate gate;
    CHECK(vbo::gateAcross(g, gate), "gate across");
    h.sectors.push_back(gate);
    double w = geo::distanceM(gate.lat1, gate.lon1, gate.lat2, gate.lon2);
    CHECK(std::abs(w - 30) < 0.1, "gate 30 m wide: %.2f", w);
  }
  std::string text = vbo::headerText(h);
  CHECK(text.find("\xC2\xAC T1\r\n") != std::string::npos && text.find("\xC2\xAC T4\r\n") != std::string::npos,
        "[laptiming] names the sector gates T1..T4");
  TrackPoint none = {SF_LAT, SF_LON, 15, 0, HeadingFrom::None};
  vbo::Gate g;
  CHECK(!vbo::gateAcross(none, g), "no direction, no gate");
  printf("  timing: %zu sector gates, every lap after timed through them, in the VBO as T1..T%zu\n", gates.size(),
         gates.size());
}

int main() {
  printf("Automatic corner sectors\n");
  testCutsOnSyntheticTrack();
  testMerging();
  testTiming();
  if (failures) {
    printf("\n%d FAILURE(S)\n", failures);
    return 1;
  }
  printf("\nALL PASSED\n");
  return 0;
}
