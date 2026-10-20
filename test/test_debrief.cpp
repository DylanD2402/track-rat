/**
 * Host-side tests for the theoretical best and the pit debrief (no ESP32
 * needed): sector times, the best in each sector from clean laps only, gaps
 * that add up to best - theoretical, the three biggest, the speed-trace
 * hints, and the paths LapTimer keeps for them.
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_debrief.cpp src/LapTimer.cpp -o /tmp/db && /tmp/db
 */

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "Debrief.h"
#include "LapTimer.h"
#include "check.h"

using namespace debrief;

// A lap from its sector times: the gates are the running totals.
static LapIn lapOf(uint16_t number, const std::vector<int32_t>& sectors, uint8_t tags = 0) {
  LapIn l = {number, 0, tags, {}};
  int32_t t = 0;
  for (size_t i = 0; i < sectors.size(); i++) {
    t += sectors[i];
    if (i + 1 < sectors.size()) l.gateMs.push_back(t);
  }
  l.timeMs = t;
  return l;
}

static void testSectorTimes() {
  std::vector<int32_t> st = sectorTimes(90000, {20000, 50000, 70000}, 4);
  CHECK(st.size() == 4 && st[0] == 20000 && st[1] == 30000 && st[2] == 20000 && st[3] == 20000, "four sectors");
  CHECK(sectorTimes(90000, {20000, -1, 70000}, 4).empty(), "a missed gate: no sector times");
  CHECK(sectorTimes(90000, {50000, 20000, 70000}, 4).empty(), "gates out of order");
  CHECK(sectorTimes(60000, {20000, 50000, 70000}, 4).empty(), "a gate after the lap ended");
  CHECK(sectorTimes(90000, {20000, 50000}, 4).empty(), "the wrong number of gates");
  CHECK(sectorTimes(90000, {}, 0).empty(), "no sectors");
  printf("  sector times: add up to the lap; missed or muddled gates give none\n");
}

static void testAnalyse() {
  std::vector<LapIn> laps = {
      lapOf(1, {25000, 31000, 21000, 24000}),                          // 101.0
      lapOf(2, {24500, 30200, 20900, 23800}),                          // 99.4: the best lap
      lapOf(3, {24000, 30900, 21500, 23700}),                          // 100.1: best T1, T4
      lapOf(4, {22000, 29000, 20000, 22000}, laptags::ODD_DISTANCE),   // a cut lap: never counts
      lapOf(5, {24900, 29900, 21300, 24100}),                          // 100.2: best T2
      lapOf(6, {25500, 30100, 20600, 400000}, laptags::PIT),           // a pit lap: left out
  };
  LapIn missed = lapOf(7, {24000, 30000, 20000, 23000});
  missed.gateMs[1] = -1;  // a missed gate: left out
  laps.push_back(missed);

  Result r = analyse(laps, 4);
  CHECK(r.valid && r.bestIndex == 1 && r.bestMs == 99400, "best is lap 2: %u", r.bestMs);
  CHECK(r.lapsUsed == 4 && r.lapsLeftOut == 3, "4 laps used, 3 left out: %d %d", r.lapsUsed, r.lapsLeftOut);
  CHECK(r.best[0].ms == 24000 && r.best[0].lap == 3, "T1 best on lap 3");
  CHECK(r.best[1].ms == 29900 && r.best[1].lap == 5, "T2 best on lap 5");
  CHECK(r.best[2].ms == 20900 && r.best[2].lap == 2, "T3 best on lap 2 (the best lap)");
  CHECK(r.best[3].ms == 23700 && r.best[3].lap == 3, "T4 best on lap 3");
  CHECK(r.theoreticalMs == 24000 + 29900 + 20900 + 23700, "theoretical %u", r.theoreticalMs);
  int32_t sum = 0;
  for (int32_t g : r.gaps) sum += g;
  CHECK(sum == (int32_t)(r.bestMs - r.theoreticalMs), "gaps add up: %d vs %u", sum, r.bestMs - r.theoreticalMs);
  CHECK(r.top.size() == 3 && r.top[0] == 0 && r.top[1] == 1 && r.top[2] == 3, "top: T1 0.5, T2 0.3, T4 0.1");
  CHECK(r.gaps[2] == 0, "no gap where the best lap was best");

  // Only one sector lost: one in the top list
  Result one = analyse({lapOf(1, {10000, 10000}), lapOf(2, {9000, 10500})}, 2);
  CHECK(one.valid && one.bestIndex == 1 && one.top.size() == 1 && one.top[0] == 1 && one.gaps[1] == 500,
        "one sector lost");
  // The best lap best everywhere: nothing on the table
  Result none = analyse({lapOf(1, {10000, 10000}), lapOf(2, {10500, 10500})}, 2);
  CHECK(none.valid && none.top.empty() && none.theoreticalMs == none.bestMs, "nothing on the table");
  // No clean lap with every sector
  CHECK(!analyse({lapOf(1, {10000, 10000}, laptags::PIT)}, 2).valid, "no clean lap: nothing to say");
  CHECK(!analyse(laps, 0).valid, "no sectors");

  // All time: the stored bests merged with this session's
  std::vector<int32_t> stored = {23500, 30500, -1, 23800};
  std::vector<int32_t> ever = mergeBest(stored, r);
  CHECK(ever[0] == 23500 && ever[1] == 29900 && ever[2] == 20900 && ever[3] == 23700, "merged");
  CHECK(sumOf(ever) == 23500 + 29900 + 20900 + 23700, "all-time theoretical");
  CHECK(mergeBest({1, 2, 3}, r)[0] == 24000, "stored for other gates: ignored");
  CHECK(sumOf({1000, -1}) == 0 && sumOf({}) == 0, "incomplete: no sum");
  printf("  analyse: clean laps only, gaps add up to %s s, top three T%d T%d T%d\n",
         seconds(r.bestMs - r.theoreticalMs).c_str(), r.top[0] + 1, r.top[1] + 1, r.top[2] + 1);
}

static void testGates() {
  std::vector<TrackPoint> gates = {{45.0, -76.0, 15, 0, HeadingFrom::Set}, {45.001, -76.0, 15, 90, HeadingFrom::Set}};
  CHECK(sameGates({{45.0, -76.0}, {45.001, -76.0}}, gates), "same gates");
  CHECK(sameGates({{45.00003, -76.0}, {45.001, -76.0}}, gates), "3 m away: same");
  CHECK(!sameGates({{45.0001, -76.0}, {45.001, -76.0}}, gates), "11 m away: other gates");
  CHECK(!sameGates({{45.0, -76.0}}, gates), "another number of gates");
}

// A sector's speed trace: brake from `peak` at `brakeAt` m down to `minV`, then accelerate to `exitV` at 200 m.
static Trace corner(float peak, float brakeAt, float minV, float exitV) {
  Trace t;
  for (float d = 0; d <= 200; d += 2) {
    float v;
    if (d < brakeAt) v = peak;
    else if (d < brakeAt + 40) v = peak - (peak - minV) * (d - brakeAt) / 40;
    else if (d < brakeAt + 60) v = minV;
    else v = minV + (exitV - minV) * (d - brakeAt - 60) / (200 - brakeAt - 60);
    t.distM.push_back(d);
    t.speedKmh.push_back(v);
  }
  return t;
}

static void testHints() {
  Trace best = corner(200, 80, 90, 160);
  Hint h = hint(corner(200, 70, 90, 160), best);
  CHECK(h.kind == HintKind::Braking && fabsf(h.value - 10) < 2.5f, "braked 10 m earlier: %d %.1f", (int)h.kind, h.value);
  h = hint(corner(200, 80, 84, 158), best);
  CHECK(h.kind == HintKind::MinSpeed && fabsf(h.value - 6) < 0.5f, "6 km/h slower at the apex: %d %.1f", (int)h.kind, h.value);
  h = hint(corner(200, 80, 90, 152), best);
  CHECK(h.kind == HintKind::Exit && fabsf(h.value - 8) < 0.5f, "8 km/h slower out: %d %.1f", (int)h.kind, h.value);
  CHECK(hint(best, best).kind == HintKind::None, "the same lap: no hint");
  CHECK(hint(corner(200, 90, 95, 165), best).kind == HintKind::None, "better everywhere: no hint");
  CHECK(hint(Trace(), best).kind == HintKind::None, "no trace: no hint");

  CHECK(hintText({HintKind::Braking, 8.2f}) == "braked 8 m early", "text: braking");
  CHECK(hintText({HintKind::MinSpeed, 4}) == "4 km/h slower at the apex", "text: slowest point");
  CHECK(hintText({HintKind::Exit, 3}) == "3 km/h slower on exit", "text: exit");
  CHECK(hintText({HintKind::Exit, 8.05f}, true) == "5 mph slower on exit", "text: mph");
  CHECK(hintText({HintKind::None, 0}).empty(), "text: none");
  CHECK(seconds(310) == "0.31" && seconds(999) == "1.00" && seconds(2995) == "3.00" && seconds(700) == "0.70" &&
            seconds(-1250) == "-1.25",
        "seconds: %s %s %s", seconds(310).c_str(), seconds(999).c_str(), seconds(2995).c_str());
  printf("  hints: braking point, slowest point, exit speed, weighed by what they cost\n");
}

// LapTimer keeps, for each sector, the path of the clean lap that was quickest there.
static void testSectorPaths() {
  const double SF_LAT = 45.0, SF_LON = -76.0, R = 150.0;
  auto pos = [&](double a, double& lat, double& lon) {
    lat = SF_LAT + R * sin(a) / 111194.93;
    lon = SF_LON + R * (cos(a) - 1.0) / (111194.93 * cos(SF_LAT * geo::DEG2RAD));
  };
  // Gates a quarter and half way round (anticlockwise), heading west and south
  std::vector<TrackPoint> gates(2);
  gates[0] = {0, 0, 15, 270, HeadingFrom::Set};
  gates[1] = {0, 0, 15, 180, HeadingFrom::Set};
  pos(M_PI / 2, gates[0].lat, gates[0].lon);
  pos(M_PI, gates[1].lat, gates[1].lon);
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15, 0, HeadingFrom::Set}, {}, MIN_LAP_TIME_MS, gates);
  uint32_t t = 12 * 3600000u;
  double a = -0.3;
  // Laps: speeds in the first half / second half
  const double speeds[][2] = {{27, 27}, {29, 26}, {26, 29}, {28.5, 28.5}};  // none slow (over 107%)
  auto drive = [&](double to, double v) {
    while (a < to) {
      double lat, lon;
      pos(a, lat, lon);
      lt.onFix(lat, lon, (float)(v * 3.6), t, t);
      t += 100;
      a += v * 0.1 / R;
    }
  };
  drive(0.02, 25);
  for (int k = 0; k < 4; k++) {
    drive(2 * M_PI * k + M_PI, speeds[k][0]);
    drive(2 * M_PI * (k + 1) + 0.02, speeds[k][1]);
  }
  CHECK(lt.lapCount() == 4, "4 laps: %zu", lt.lapCount());
  std::vector<LapIn> laps;
  for (size_t i = 0; i < lt.lapCount(); i++)
    laps.push_back({lt.lap(i).number, lt.lap(i).timeMs, lt.lap(i).tags, lt.lap(i).sectorMs});
  Result r = analyse(laps, 3);
  CHECK(r.valid && r.bestIndex == 3 && r.lapsUsed == 4, "lap 4 (28.5 m/s all round) is the best: %d", r.bestIndex + 1);
  for (size_t s = 0; s < 3; s++) {
    CHECK(lt.sectorBestMs(s) == r.best[s].ms, "T%zu: the timer's best %d = the debrief's %d", s + 1, lt.sectorBestMs(s),
          r.best[s].ms);
    CHECK(lt.sectorBestPath(s).size() > 10, "T%zu has a path", s + 1);
  }
  CHECK(r.best[0].lap == 2 && r.best[2].lap == 3, "T1 best on lap 2, T3 on lap 3: %u %u", r.best[0].lap, r.best[2].lap);
  CHECK(lt.bestPathLapIndex() == 3, "the best lap's path is kept");
  // Lap 4 against lap 3 through T3: slower all the way, so a speed hint
  const std::vector<int32_t>& g = lt.lap(3).sectorMs;
  Trace mine = slice(lt.bestPath(), g[1], (int32_t)lt.lap(3).timeMs);
  Trace other = slice(lt.sectorBestPath(2), INT32_MIN, INT32_MAX);
  Hint h = hint(mine, other);
  CHECK(h.kind == HintKind::MinSpeed || h.kind == HintKind::Exit, "a speed hint for T3: %d", (int)h.kind);
  CHECK(fabsf(h.value - 1.8f) < 0.6f, "0.5 m/s slower: %.1f km/h", h.value);
  lt.setSectors(gates);
  CHECK(lt.sectorBestMs(0) == -1 && lt.sectorBestPath(0).empty(), "new gates: the bests start again");
  printf("  the lap timer keeps each sector's quickest path for the hints\n");
}

int main() {
  printf("Theoretical best and the pit debrief\n");
  testSectorTimes();
  testAnalyse();
  testGates();
  testHints();
  testSectorPaths();
  if (failures) {
    printf("\n%d FAILURE(S)\n", failures);
    return 1;
  }
  printf("\nALL PASSED\n");
  return 0;
}
