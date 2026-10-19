/**
 * Host-side tests for acceleration and braking runs.
 *
 *   g++ -std=c++17 -Iinclude test/test_perfrun.cpp -o /tmp/pr && /tmp/pr
 */

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>
#include "PerfRun.h"

using perf::PerfResult;
using perf::PerfRun;
using perf::RunKind;

static int failures = 0;
#define CHECK(cond, ...)                          \
  do {                                            \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

// Within `tolMs` milliseconds of `expectS` seconds.
#define CHECK_S(got, expectS, tolMs, what)                                                              \
  CHECK(std::fabs((got) - (expectS)) * 1000.0 <= (tolMs), "%s: got %.4f s, want %.4f s (tol %.1f ms)", what, \
        (double)(got), (double)(expectS), (double)(tolMs))

typedef std::function<double(double)> Fn;

// Feed a car whose speed (m/s) and height (m) are functions of time, from t=0 to `endS`,
// with a fix every 1000/hz ms starting `phaseMs` in. Fixes for which `drop` is true are lost.
static void drive(PerfRun& pr, Fn speed, Fn alt, double endS, int hz, int phaseMs, uint32_t baseGpsMs = 3600000,
                  std::function<bool(double)> drop = nullptr) {
  const int period = 1000 / hz;
  for (int ms = phaseMs; ms <= endS * 1000; ms += period) {
    double t = ms / 1000.0;
    if (drop && drop(t)) continue;
    uint32_t gps = (uint32_t)((baseGpsMs + ms) % 86400000UL);
    pr.onFix(gps, (float)(speed(t) * 3.6), (float)alt(t));
  }
}

static Fn flat() { return [](double) { return 100.0; }; }

// Stopped until `tl`, then constant acceleration `a` up to `vmax`, then constant.
static Fn launchProfile(double tl, double a, double vmax) {
  return [=](double t) { return t <= tl ? 0.0 : std::fmin(a * (t - tl), vmax); };
}

static const double MPH60 = 60 * perf::KMH_PER_MPH / 3.6;  // m/s

static void testConstantAcceleration(int hz) {
  // 5 m/s^2 from a standing start at 2.2345 s up to 70 m/s (252 km/h), held for a few seconds.
  const double a = 5.0, tl = 2.2345, vmax = 70.0;
  int worstPhase = 0;
  double worstErr = 0;
  for (int phase = 0; phase < 1000 / hz; phase += 7) {
    PerfRun pr;
    drive(pr, launchProfile(tl, a, vmax), flat(), 22, hz, phase);
    CHECK(pr.results().size() == 1, "%d Hz phase %d: %zu results", hz, phase, pr.results().size());
    if (pr.results().empty()) continue;
    const PerfResult& r = pr.results()[0];
    CHECK(r.kind == RunKind::Launch && r.valid && !r.notLevel, "%d Hz: kind/valid/level", hz);
    const double tol = 1.5;  // ms
    CHECK_S(r.t0to60, 60 / 3.6 / a, tol, "0-60 km/h");
    CHECK_S(r.t0to100, 100 / 3.6 / a, tol, "0-100 km/h");
    CHECK_S(r.t0to60mph, MPH60 / a, tol, "0-60 mph");
    CHECK_S(r.t100to200, 100 / 3.6 / a, tol, "100-200 km/h");
    CHECK_S(r.et8, std::sqrt(2 * perf::EIGHTH_MILE_M / a), tol, "1/8 mile");
    CHECK_S(r.et4, std::sqrt(2 * perf::QUARTER_MILE_M / a), tol, "1/4 mile");
    double trap8 = a * std::sqrt(2 * perf::EIGHTH_MILE_M / a) * 3.6;
    double trap4 = a * std::sqrt(2 * perf::QUARTER_MILE_M / a) * 3.6;
    CHECK(std::fabs(r.trap8Kmh - trap8) < 0.1, "1/8 trap %.2f want %.2f", r.trap8Kmh, trap8);
    CHECK(std::fabs(r.trap4Kmh - trap4) < 0.1, "1/4 trap %.2f want %.2f", r.trap4Kmh, trap4);
    CHECK(std::fabs(r.peakKmh - vmax * 3.6) < 1.0, "peak %.1f", r.peakKmh);
    double err = std::fabs(r.t0to100 - 100 / 3.6 / a) * 1000;
    if (err > worstErr) {
      worstErr = err;
      worstPhase = phase;
    }
  }
  printf("  constant 5 m/s^2 launch at %d Hz: 0-100 %.3f s, worst error %.2f ms (phase %d)\n", hz, 100 / 3.6 / a,
         worstErr, worstPhase);
}

static void testRollout(int hz) {
  const double a = 5.0, tl = 2.2345;
  PerfRun pr;
  pr.setRollout(true);
  drive(pr, launchProfile(tl, a, 70), flat(), 22, hz, 13);
  CHECK(pr.results().size() == 1, "rollout: %zu results", pr.results().size());
  if (pr.results().empty()) return;
  const PerfResult& r = pr.results()[0];
  const double roll = std::sqrt(2 * perf::METRES_PER_FOOT / a);  // 0.349 s to cover a foot
  CHECK(r.rollout, "rollout flag");
  CHECK_S(r.t0to100, 100 / 3.6 / a - roll, 1.5, "0-100 with rollout");
  CHECK_S(r.t0to60mph, MPH60 / a - roll, 1.5, "0-60 mph with rollout");
  CHECK_S(r.et4, std::sqrt(2 * perf::QUARTER_MILE_M / a) - roll, 1.5, "1/4 mile with rollout");
  CHECK_S(r.t100to200, 100 / 3.6 / a, 1.5, "100-200 is not affected by rollout");
  printf("  rollout at %d Hz: 1 ft takes %.3f s, 0-100 %.3f s\n", hz, roll, r.t0to100);
}

// A realistic curve: acceleration falls off as speed rises. The straight-line interpolation between
// fixes is slightly off here, so the tolerance is a little wider than for constant acceleration.
static void testRealisticCurve(int hz, double tolMs) {
  const double a0 = 8.0, vmax = 70.0, tau = vmax / a0, tl = 2.5;
  auto vel = [=](double t) { return t <= tl ? 0.0 : vmax * (1 - std::exp(-(t - tl) / tau)); };
  auto dist = [=](double t) { return t <= tl ? 0.0 : vmax * ((t - tl) - tau * (1 - std::exp(-(t - tl) / tau))); };
  auto timeAtSpeed = [=](double v) { return -tau * std::log(1 - v / vmax); };
  auto timeAtDist = [=](double d) {
    double lo = 0, hi = 60;
    for (int i = 0; i < 80; i++) {
      double mid = (lo + hi) / 2;
      (dist(tl + mid) < d ? lo : hi) = mid;
    }
    return lo;
  };
  PerfRun pr;
  // (a real car doesn't rise forever: it lifts at 30 s)
  drive(pr, [=](double t) { return t < 30 ? vel(t) : std::fmax(0.0, vel(30) - 3.0 * (t - 30)); }, flat(), 40, hz, 11);
  CHECK(pr.results().size() == 1, "curve: %zu results", pr.results().size());
  if (pr.results().empty()) return;
  const PerfResult& r = pr.results()[0];
  CHECK_S(r.t0to60, timeAtSpeed(60 / 3.6), tolMs, "curve 0-60 km/h");
  CHECK_S(r.t0to100, timeAtSpeed(100 / 3.6), tolMs, "curve 0-100 km/h");
  CHECK_S(r.t0to60mph, timeAtSpeed(MPH60), tolMs, "curve 0-60 mph");
  CHECK_S(r.t100to200, timeAtSpeed(200 / 3.6) - timeAtSpeed(100 / 3.6), tolMs, "curve 100-200 km/h");
  CHECK_S(r.et8, timeAtDist(perf::EIGHTH_MILE_M), tolMs, "curve 1/8 mile");
  CHECK_S(r.et4, timeAtDist(perf::QUARTER_MILE_M), tolMs, "curve 1/4 mile");
  printf("  realistic curve at %d Hz: 0-100 %.3f s (want %.3f), 1/4 mile %.3f s (want %.3f)\n", hz, r.t0to100,
         timeAtSpeed(100 / 3.6), r.et4, timeAtDist(perf::QUARTER_MILE_M));
}

static void testBraking(int hz) {
  // Cruise at 130 km/h, then brake at 9 m/s^2 to a stop, then sit.
  const double v0 = 130 / 3.6, dec = 9.0, tb = 2.0, tstop = tb + v0 / dec;
  auto vel = [=](double t) { return t <= tb ? v0 : std::fmax(0.0, v0 - dec * (t - tb)); };
  const double v100 = 100 / 3.6;
  const double wantM = v100 * v100 / (2 * dec), wantS = v100 / dec;
  double worstM = 0, worstMs = 0;
  for (int phase = 0; phase < 1000 / hz; phase += 9) {
    PerfRun pr;
    drive(pr, vel, flat(), tstop + 3, hz, phase);
    CHECK(pr.results().size() == 1, "%d Hz phase %d braking: %zu results", hz, phase, pr.results().size());
    if (pr.results().empty()) continue;
    const PerfResult& r = pr.results()[0];
    CHECK(r.kind == RunKind::Brake && r.valid, "brake kind");
    worstM = std::fmax(worstM, std::fabs(r.brakeM - wantM));
    worstMs = std::fmax(worstMs, std::fabs(r.brakeS - wantS) * 1000);
    CHECK(std::fabs(r.brakeM - wantM) < 0.15, "%d Hz phase %d: brake %.2f m want %.2f m", hz, phase, r.brakeM, wantM);
    CHECK_S(r.brakeS, wantS, 4.0, "brake time");
  }
  printf("  100-0 km/h at %d Hz: %.2f m in %.3f s; worst error %.3f m, %.2f ms\n", hz, wantM, wantS, worstM, worstMs);
}

static void testRolling100to200() {
  // Cruise at 60 km/h, then accelerate at 2.5 m/s^2 to 220 km/h. Not armed, so no launch run.
  const double a = 2.5, v0 = 60 / 3.6, tl = 3.0, vmax = 220 / 3.6;
  auto vel = [=](double t) { return std::fmin(t <= tl ? v0 : v0 + a * (t - tl), vmax); };
  for (int hz : {10, 20}) {
    PerfRun pr;
    drive(pr, vel, flat(), 40, hz, 3);
    CHECK(pr.results().size() == 1, "rolling: %zu results", pr.results().size());
    if (pr.results().empty()) continue;
    const PerfResult& r = pr.results()[0];
    CHECK(r.kind == RunKind::Roll, "rolling run is a Roll");
    CHECK_S(r.t100to200, 100 / 3.6 / a, 1.5, "rolling 100-200");
    CHECK(r.t0to100 < 0 && r.et4 < 0, "a rolling run has no 0-100 or 1/4 mile");
  }
  printf("  100-200 km/h from a rolling start: 11.1 s\n");
}

// Launch, hold, brake to a stop, wait, launch again: launch + brake + launch.
static void testSeveralRuns() {
  const double a = 6.0, dec = 8.0, tl = 2.3, vtop = 130 / 3.6;
  const double tHold = tl + vtop / a + 2.0;         // 2 s at 130 km/h
  const double tStop = tHold + vtop / dec;
  const double tl2 = tStop + 3.5;
  auto vel = [=](double t) {
    if (t <= tl) return 0.0;
    if (t <= tl + vtop / a) return a * (t - tl);
    if (t <= tHold) return vtop;
    if (t <= tStop) return vtop - dec * (t - tHold);
    if (t <= tl2) return 0.0;
    return std::fmin(a * (t - tl2), vtop);
  };
  PerfRun pr;
  drive(pr, vel, flat(), tl2 + 14, 20, 0);
  const auto& rs = pr.results();
  CHECK(rs.size() == 3, "several runs: %zu results", rs.size());
  if (rs.size() != 3) return;
  CHECK(rs[0].kind == RunKind::Launch && rs[1].kind == RunKind::Brake && rs[2].kind == RunKind::Launch, "kinds");
  CHECK_S(rs[0].t0to100, 100 / 3.6 / a, 1.5, "run 1 0-100");
  CHECK_S(rs[2].t0to100, 100 / 3.6 / a, 1.5, "run 2 0-100");
  CHECK(rs[0].t100to200 < 0, "never reached 200");
  PerfResult b = pr.best();
  CHECK_S(b.t0to100, 100 / 3.6 / a, 1.5, "best 0-100");
  CHECK(rs[0].et8 < 0, "run 1 stopped accelerating before 1/8 mile");
  CHECK(rs[2].et8 > 0, "run 2 held 130 km/h long enough for the 1/8 mile");
  CHECK(b.brakeM > 0, "best brake present");
  CHECK(pr.state() == PerfRun::State::Waiting || pr.state() == PerfRun::State::Armed, "state after runs");
  printf("  launch, brake, launch: %zu results, best 0-100 %.3f s\n", rs.size(), b.t0to100);
}

static void testGap() {
  const double a = 5.0, tl = 2.5;
  // 1.2 s with no fixes while accelerating
  PerfRun pr;
  drive(pr, launchProfile(tl, a, 70), flat(), 22, 20, 0, 3600000, [](double t) { return t > 6.0 && t < 7.2; });
  CHECK(!pr.results().empty() && pr.results()[0].kind == RunKind::Launch && !pr.results()[0].valid,
        "launch across a gap is invalid");
  bool anyValidLaunch = false;
  for (const PerfResult& r : pr.results()) anyValidLaunch |= (r.kind == RunKind::Launch && r.valid);
  CHECK(!anyValidLaunch, "no valid launch after a gap");
  CHECK(pr.best().t0to100 < 0, "an invalid run doesn't count towards the best");

  // A short drop (3 fixes at 20 Hz = 150 ms) is bridged by interpolation
  PerfRun pr2;
  drive(pr2, launchProfile(tl, a, 70), flat(), 22, 20, 0, 3600000, [](double t) { return t > 5.0 && t < 5.18; });
  CHECK(pr2.results().size() == 1 && pr2.results()[0].valid, "a 150 ms drop-out is fine");
  if (!pr2.results().empty()) CHECK_S(pr2.results()[0].t0to100, 100 / 3.6 / a, 1.5, "0-100 over a short drop-out");

  // A gap before the run only costs the arming time
  PerfRun pr3;
  drive(pr3, launchProfile(tl + 4, a, 70), flat(), 26, 20, 0, 3600000, [](double t) { return t > 1.0 && t < 3.5; });
  CHECK(pr3.results().size() == 1 && pr3.results()[0].valid, "gap before the run: %zu results", pr3.results().size());
  printf("  gaps: a 1.2 s hole invalidates the run, a 150 ms one doesn't\n");
}

static void testLevel() {
  const double a = 5.0, tl = 2.5;
  // 5% uphill: 0-100 covers ~77 m, so 3.9 m of climb by 100 km/h, and ~20 m by the 1/4 mile.
  PerfRun hill;
  auto climb = [=](double t) {
    double x = t <= tl ? 0 : std::fmin(0.5 * a * (t - tl) * (t - tl), 1e9);
    return 100.0 + 0.05 * x;
  };
  drive(hill, launchProfile(tl, a, 70), climb, 22, 20, 0);
  CHECK(hill.results().size() == 1, "hill: %zu results", hill.results().size());
  if (!hill.results().empty()) {
    const PerfResult& r = hill.results()[0];
    CHECK(r.notLevel, "5%% hill is flagged (climb %.1f m, grade %.1f%%)", r.climbM, r.gradePct);
    CHECK(r.climbM > 10 && std::fabs(r.gradePct - 5.0) < 1.0, "climb %.1f m grade %.2f%%", r.climbM, r.gradePct);
  }

  // Level road with +/-1 m of GPS height noise: no warning
  PerfRun noisy;
  int n = 0;
  drive(noisy, launchProfile(tl, a, 70), [&](double) { return 100.0 + ((n++ * 7919) % 21 - 10) / 10.0; }, 22, 20, 0);
  CHECK(noisy.results().size() == 1 && !noisy.results()[0].notLevel, "noisy but level road is not flagged");
  if (!noisy.results().empty()) printf("  level: 5%% hill flagged (%.1f m climb), noisy flat road not (%.1f m)\n",
                                       hill.results().empty() ? 0.0 : hill.results()[0].climbM,
                                       noisy.results()[0].climbM);
}

static void testStartRules() {
  // Pulling away 0.5 s after stopping isn't an armed start: the run is skipped.
  const double a = 5.0;
  auto prof = [=](double t) {
    if (t < 3.0) return 10.0;                       // 36 km/h
    if (t < 3.0 + 10.0 / 5.0) return 10.0 - 5.0 * (t - 3.0);  // stop at t = 5
    if (t < 5.5) return 0.0;                        // only 0.5 s stopped
    return std::fmin(a * (t - 5.5), 70.0);
  };
  PerfRun pr;
  drive(pr, prof, flat(), 30, 20, 0);
  int launches = 0;
  for (const PerfResult& r : pr.results()) launches += (r.kind == RunKind::Launch);
  CHECK(launches == 0, "a start 0.5 s after stopping isn't timed as a launch (%d)", launches);

  // A creep forward and back doesn't count as a run and doesn't stop the next one
  auto creep = [=](double t) {
    if (t < 2.0) return 0.0;
    if (t < 2.5) return 2.0 / 3.6 * (t - 2.0) / 0.5;  // up to 2 km/h
    if (t < 3.0) return 2.0 / 3.6 * (3.0 - t) / 0.5;  // and back to a stop
    if (t < 5.0) return 0.0;
    return std::fmin(a * (t - 5.0), 70.0);
  };
  PerfRun pc;
  drive(pc, creep, flat(), 28, 20, 0);
  CHECK(pc.results().size() == 1, "after a creep: %zu results", pc.results().size());
  if (!pc.results().empty()) CHECK_S(pc.results()[0].t0to100, 100 / 3.6 / a, 1.5, "0-100 after a creep");

  // State shown on screen: waiting, then armed after 1 s stopped, then go
  PerfRun st;
  std::vector<PerfRun::State> seen;
  const uint32_t base = 5000;
  st.onFix(base, 0, 100);
  CHECK(st.state() == PerfRun::State::Waiting, "waiting at first");
  for (int i = 1; i <= 19; i++) st.onFix(base + i * 50, 0, 100);
  CHECK(st.state() == PerfRun::State::Waiting, "waiting before 1 s");
  st.onFix(base + 1000, 0, 100);
  CHECK(st.state() == PerfRun::State::Armed, "armed after 1 s stopped");
  st.onFix(base + 1050, 3, 100);
  CHECK(st.state() == PerfRun::State::Go, "go on movement");
  printf("  start rules: arming after 1 s, no start 0.5 s after stopping, a creep is ignored\n");
}

static void testMidnight() {
  const double a = 5.0, tl = 2.2345;
  PerfRun pr;
  drive(pr, launchProfile(tl, a, 70), flat(), 22, 10, 0, 86400000UL - 6000);  // GPS clock wraps at 6 s
  CHECK(pr.results().size() == 1, "midnight: %zu results", pr.results().size());
  if (!pr.results().empty()) {
    CHECK_S(pr.results()[0].t0to100, 100 / 3.6 / a, 1.5, "0-100 over midnight");
    CHECK(pr.results()[0].startGpsMs < 86400000UL, "start time is a time of day");
  }
  printf("  run across UTC midnight: ok\n");
}

// The same car as GPSHandler::simulateLaunchStep (the bench demo): stand 4 s, pull away, brake from 215 km/h.
// Two cycles should give launch, brake, launch, brake with the times the curve predicts.
static void testBenchDemoCar() {
  const double dt = 0.05, vmax = 70.0, a0 = 8.0, vEnd = 215 / 3.6, brake = 9.5;
  PerfRun pr;
  int phase = 0;
  double phaseT = 0, v = 0, clockMs = 0;
  for (int i = 0; i < 20 * 100; i++) {
    phaseT += dt;
    if (phase == 0) {
      v = 0;
      if (phaseT >= 4.0) phase = 1, phaseT = 0;
    } else if (phase == 1) {
      v = vmax * (1 - std::exp(-a0 * phaseT / vmax));
      if (v >= vEnd) phase = 2, phaseT = 0;
    } else {
      v -= brake * dt;
      if (v <= 0) v = 0, phase = 0, phaseT = 0;
    }
    clockMs += 50;
    pr.onFix((uint32_t)((clockMs + 12 * 3600000UL)), (float)(v * 3.6), 100.0f);
  }
  const auto& rs = pr.results();
  CHECK(rs.size() >= 4, "bench demo: %zu results in 100 s", rs.size());
  if (rs.size() < 4) return;
  CHECK(rs[0].kind == RunKind::Launch && rs[1].kind == RunKind::Brake && rs[2].kind == RunKind::Launch, "demo kinds");
  const double tau = vmax / a0;
  CHECK_S(rs[0].t0to100, -tau * std::log(1 - (100 / 3.6) / vmax), 60.0, "demo 0-100");  // the demo steps in 50 ms
  CHECK(rs[0].t100to200 > 0 && rs[0].et8 > 0 && rs[0].et4 > 0, "demo launch reaches 200 and the 1/4 mile");
  CHECK(rs[1].brakeM > 0, "demo brake");
  printf("  bench demo car: 0-100 %.2f s, 100-200 %.2f s, 1/4 mile %.2f s @ %.0f km/h, 100-0 %.1f m\n", rs[0].t0to100,
         rs[0].t100to200, rs[0].et4, rs[0].trap4Kmh, rs[1].brakeM);
}

static void testUnits() {
  CHECK(std::fabs(perf::speedInUnit(160.9344f, perf::SpeedUnit::Mph) - 100.0f) < 0.001f, "km/h to mph");
  CHECK(perf::speedInUnit(100.0f, perf::SpeedUnit::Kmh) == 100.0f, "km/h unchanged");
}

int main() {
  printf("PerfRun tests\n");
  testConstantAcceleration(10);
  testConstantAcceleration(20);
  testRollout(10);
  testRollout(20);
  testRealisticCurve(20, 3.0);
  testRealisticCurve(10, 8.0);
  testBraking(10);
  testBraking(20);
  testRolling100to200();
  testSeveralRuns();
  testGap();
  testLevel();
  testStartRules();
  testMidnight();
  testBenchDemoCar();
  testUnits();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
