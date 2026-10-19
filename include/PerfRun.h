/**
 * Track Rat - Acceleration and braking runs
 *
 * Fed one GPS fix at a time (GPS time, speed, height). Finds three kinds of run:
 *
 *   Launch  Armed once the car has been stopped (under 1 km/h) for 1 s. Starts
 *           on movement. Times 0-60 and 0-100 km/h, 0-60 mph, 100-200 km/h,
 *           1/8 and 1/4 mile (elapsed time and trap speed). With rollout on,
 *           the clock starts after the car has moved 1 foot (the drag-strip
 *           convention); distances are still from where it started.
 *   Roll    100-200 km/h started from a moving car, when no launch is running.
 *   Brake   100-0 km/h braking distance and time, from the moment speed falls
 *           through 100 km/h to the stop. Works from a rolling start too.
 *
 * Times and distances are interpolated between fixes, so they are good to a few
 * milliseconds at 10 Hz and better at 20 Hz. Distance is the integral of GPS
 * speed, which is much smoother than differencing positions. A run that spans
 * a gap in the fixes is marked invalid. A run whose height changed by more than
 * a few metres is marked not level (GPS height is noisy, so small climbs are
 * ignored).
 *
 * No Arduino dependencies: unit-tested on a PC in test/test_perfrun.cpp.
 */

#ifndef PERF_RUN_H
#define PERF_RUN_H

#include <math.h>
#include <cmath>
#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace perf {

constexpr double KMH_PER_MPH = 1.609344;
constexpr double METRES_PER_FOOT = 0.3048;
constexpr double EIGHTH_MILE_M = 201.168;
constexpr double QUARTER_MILE_M = 402.336;
constexpr float NA = -1.0f;  // a measurement that was not reached

// Display units. Results are always stored in km/h, metres and seconds.
enum class SpeedUnit { Kmh, Mph };
inline float speedInUnit(float kmh, SpeedUnit u) { return u == SpeedUnit::Mph ? (float)(kmh / KMH_PER_MPH) : kmh; }
inline const char* speedLabel(SpeedUnit u) { return u == SpeedUnit::Mph ? "mph" : "km/h"; }

enum class RunKind : uint8_t { Launch, Roll, Brake };

struct PerfResult {
  RunKind kind = RunKind::Launch;
  bool valid = true;      // false: the GPS dropped fixes during the run
  bool rollout = false;   // launch timed from 1 foot of rollout
  bool notLevel = false;  // height changed too much to trust the run
  float climbM = 0;       // height gained over the measured part (negative = downhill)
  float gradePct = 0;
  float peakKmh = 0;
  uint32_t startGpsMs = 0;  // GPS time of day when timing started

  // seconds, NA if not reached
  float t0to60 = NA, t0to100 = NA, t0to60mph = NA, t100to200 = NA;
  float et8 = NA, et4 = NA;            // 1/8 and 1/4 mile
  float trap8Kmh = NA, trap4Kmh = NA;  // speed at the end of each
  float brakeM = NA, brakeS = NA;      // 100-0 km/h
};

inline bool hasMeasurement(const PerfResult& r) {
  return r.t0to60 >= 0 || r.t0to100 >= 0 || r.t0to60mph >= 0 || r.t100to200 >= 0 || r.et8 >= 0 || r.et4 >= 0 ||
         r.brakeM >= 0;
}

class PerfRun {
public:
  static constexpr float STOP_KMH = 1.0f;          // slower than this counts as stopped
  static constexpr float START_KMH = 1.5f;         // armed car faster than this has started
  static constexpr double ARM_HOLD_S = 1.0;        // stopped this long to arm
  static constexpr uint32_t MAX_GAP_MS = 400;      // fixes further apart than this are a gap
  static constexpr float LIFT_DROP_KMH = 10.0f;    // a launch ends when speed falls this far below its peak
  static constexpr double PLATEAU_S = 3.0;         // ...or when speed has stopped rising for this long
  static constexpr double MAX_START_LOOKBACK_S = 0.25;  // how far before the first moving fix a start can be put
  static constexpr float PEAK_STEP_KMH = 0.5f;     // GPS noise below this isn't a new peak
  static constexpr float MIN_INVALID_PEAK_KMH = 30.0f;  // slower attempts that hit a gap are dropped
  static constexpr float LEVEL_MIN_CLIMB_M = 3.0f;      // GPS height is only good to a few metres
  static constexpr float LEVEL_MIN_GRADE_PCT = 1.5f;
  static constexpr float LEVEL_MIN_DIST_M = 20.0f;
  static constexpr float ROLL_FROM_KMH = 100.0f, ROLL_TO_KMH = 200.0f;
  static constexpr float BRAKE_FROM_KMH = 100.0f;
  static constexpr float BRAKE_REBOUND_KMH = 5.0f;  // speeding up again cancels a braking run
  static constexpr size_t MAX_RESULTS = 40;

  enum class State { Waiting, Armed, Go };  // Waiting = not stopped long enough yet

  void setRollout(bool on) { rollout_ = on; }  // takes effect at the next launch
  bool rollout() const { return rollout_; }

  // Feed one GPS fix. Returns how many results this fix completed (0 usually).
  size_t onFix(uint32_t gpsTimeMs, float speedKmh, float altM) {
    const size_t before = results_.size();
    if (!std::isfinite(speedKmh)) return 0;  // a corrupt fix: skip it (a long skip shows up as a gap)
    if (!std::isfinite(altM)) altM = haveAlt_ ? (float)altF_ : 0.0f;
    const double kmh = speedKmh < 0 ? 0 : speedKmh;

    bool gap = false;
    if (haveLast_) {
      uint32_t d = (gpsTimeMs + DAY_MS - lastGpsMs_) % DAY_MS;
      if (d == 0 || d > DAY_MS / 2) return 0;  // duplicate or out-of-order fix
      clock_ += d / 1000.0;
      if (d < minInterval_) minInterval_ = d;
      // Fixes normally come at the GPS update rate; a much longer wait is a gap. (A 1 Hz GPS isn't all gaps.)
      uint32_t limit = 3 * minInterval_;
      gap = d > (limit > MAX_GAP_MS ? limit : MAX_GAP_MS);
    }
    lastGpsMs_ = gpsTimeMs;

    if (!haveAlt_) {
      altF_ = altM;
      haveAlt_ = true;
    } else {
      altF_ += 0.3 * (altM - altF_);
    }
    const Pt p = {clock_, kmh / 3.6, altF_};

    if (gap) {
      failActive();
      haveLast_ = false;
      stoppedSince_ = -1;
    }

    if (kmh < STOP_KMH) {
      if (stoppedSince_ < 0) {
        stoppedSince_ = p.t;
        stopAlt_ = altM;
        stopN_ = 1;
      } else {
        if (stopN_ < 40) stopN_++;
        stopAlt_ += (altM - stopAlt_) / stopN_;
      }
    } else {
      stoppedSince_ = -1;
    }

    if (haveLast_) {
      stepRoll(p);
      stepBrake(p);
      stepLaunch(p, kmh);
    }
    last_ = p;
    haveLast_ = true;
    return results_.size() - before;
  }

  State state() const {
    if (st_ == LState::Armed) return State::Armed;
    if (st_ == LState::Starting || st_ == LState::Running) return State::Go;
    return State::Waiting;
  }
  float speedKmh() const { return haveLast_ ? (float)(last_.v * 3.6) : 0.0f; }
  // Time since timing started and distance covered, for the live screen (0 unless a launch is running).
  float liveElapsedS() const { return (st_ == LState::Running && L_.zeroSet) ? (float)(last_.t - L_.t0) : 0.0f; }
  float liveDistanceM() const { return st_ == LState::Running ? (float)L_.dist : 0.0f; }

  const std::vector<PerfResult>& results() const { return results_; }

  // The session's best of each measurement over the valid runs. kind is not meaningful.
  PerfResult best() const {
    PerfResult b;
    auto lower = [](float& cur, float v) {
      if (v >= 0 && (cur < 0 || v < cur)) cur = v;
    };
    for (const PerfResult& r : results_) {
      if (!r.valid) continue;
      lower(b.t0to60, r.t0to60);
      lower(b.t0to100, r.t0to100);
      lower(b.t0to60mph, r.t0to60mph);
      lower(b.t100to200, r.t100to200);
      if (r.et8 >= 0 && (b.et8 < 0 || r.et8 < b.et8)) {
        b.et8 = r.et8;
        b.trap8Kmh = r.trap8Kmh;
      }
      if (r.et4 >= 0 && (b.et4 < 0 || r.et4 < b.et4)) {
        b.et4 = r.et4;
        b.trap4Kmh = r.trap4Kmh;
      }
      if (r.brakeM >= 0 && (b.brakeM < 0 || r.brakeM < b.brakeM)) {
        b.brakeM = r.brakeM;
        b.brakeS = r.brakeS;
      }
      if (r.peakKmh > b.peakKmh) b.peakKmh = r.peakKmh;
    }
    return b;
  }

  void clear() {
    bool ro = rollout_;
    *this = PerfRun();
    rollout_ = ro;
  }

private:
  static constexpr uint32_t DAY_MS = 86400000UL;
  static constexpr double MS = 1.0 / 3.6;  // km/h to m/s

  struct Pt {
    double t;    // seconds on our own running clock
    double v;    // m/s
    double alt;  // smoothed, metres
  };

  enum class LState { Waiting, Armed, Starting, Running };

  struct Launch {
    PerfResult r;
    bool zeroSet = false;
    double t0 = 0;       // timing zero: movement start, or after the rollout
    double dist = 0;     // metres from where the car started to move
    double t100 = -1;    // absolute time of the 100 km/h crossing
    double peakKmh = 0, lastPeakT = 0;
    double startAlt = 0, endAlt = 0, endDist = 0;  // height at the start and at the last measurement
  };

  bool haveLast_ = false, haveAlt_ = false;
  bool rollout_ = false;
  uint32_t lastGpsMs_ = 0;
  uint32_t minInterval_ = 0xFFFFFFFFu;  // shortest time between fixes seen so far
  double clock_ = 0;
  Pt last_ = {0, 0, 0};
  double altF_ = 0;
  double stoppedSince_ = -1;
  double stopAlt_ = 0;
  int stopN_ = 0;

  LState st_ = LState::Waiting;
  Pt trig_ = {0, 0, 0};  // the first fix over START_KMH
  Launch L_;

  bool rollActive_ = false;
  double rollT100_ = 0, rollDist_ = 0, rollAlt0_ = 0;

  bool brakeActive_ = false;
  double brakeT0_ = 0, brakeDist_ = 0, brakeAlt0_ = 0, brakeMinV_ = 0;
  Pt brakePrev_ = {0, 0, 0};  // the fix before last_, while braking

  std::vector<PerfResult> results_;

  // ---- helpers ------------------------------------------------------------

  // Time into a segment at which `x` metres have been covered, starting at speed v0 with constant acceleration acc.
  static double solveTau(double v0, double acc, double x) {
    double disc = v0 * v0 + 2 * acc * x;
    if (disc < 0) disc = 0;
    double den = v0 + sqrt(disc);
    return den > 0 ? 2 * x / den : 0;
  }

  uint32_t gpsMsAt(double t) const {
    double ms = (double)lastGpsMs_ - (clock_ - t) * 1000.0;
    ms = fmod(ms, (double)DAY_MS);
    if (ms < 0) ms += DAY_MS;
    return (uint32_t)(ms + 0.5) % DAY_MS;
  }

  static void setLevel(PerfResult& r, double climb, double dist) {
    r.climbM = (float)climb;
    r.gradePct = dist > 0 ? (float)(100.0 * climb / dist) : 0.0f;
    r.notLevel = dist >= LEVEL_MIN_DIST_M && fabs(climb) >= LEVEL_MIN_CLIMB_M && fabs(r.gradePct) >= LEVEL_MIN_GRADE_PCT;
  }

  void push(const PerfResult& r) {
    if (results_.size() >= MAX_RESULTS) results_.erase(results_.begin());
    results_.push_back(r);
  }

  // The GPS dropped fixes: whatever was being measured can't be trusted.
  void failActive() {
    if (st_ == LState::Running) finishLaunch(true);
    st_ = LState::Waiting;
    if (rollActive_) {
      PerfResult r;
      r.kind = RunKind::Roll;
      r.valid = false;
      push(r);
      rollActive_ = false;
    }
    if (brakeActive_) {
      PerfResult r;
      r.kind = RunKind::Brake;
      r.valid = false;
      push(r);
      brakeActive_ = false;
    }
  }

  // ---- launch ---------------------------------------------------------------

  void stepLaunch(const Pt& p, double kmh) {
    switch (st_) {
      case LState::Waiting:
        if (stoppedSince_ >= 0 && p.t - stoppedSince_ >= ARM_HOLD_S) st_ = LState::Armed;
        break;

      case LState::Armed:
        if (kmh >= START_KMH) {
          trig_ = p;
          st_ = LState::Starting;
        }
        break;

      case LState::Starting: {
        // Two fixes after the start give the acceleration, which tells when speed was zero.
        if (kmh < STOP_KMH) {
          st_ = LState::Waiting;  // a twitch, not a start
          break;
        }
        double dt = p.t - trig_.t;
        double slope = (p.v - trig_.v) / dt;
        double t0 = slope > 0 ? trig_.t - trig_.v / slope : trig_.t;
        if (t0 < trig_.t - MAX_START_LOOKBACK_S) t0 = trig_.t - MAX_START_LOOKBACK_S;  // a creep, not a launch
        if (t0 > trig_.t) t0 = trig_.t;
        L_ = Launch();
        L_.r.kind = RunKind::Launch;
        L_.r.rollout = rollout_;
        L_.startAlt = stopAlt_;
        L_.endAlt = stopAlt_;
        L_.t0 = t0;  // movement start; moved later if rollout is on
        L_.lastPeakT = t0;
        L_.r.startGpsMs = 0;
        st_ = LState::Running;
        Pt p0 = {t0, 0, stopAlt_};
        advanceLaunch(p0, trig_);
        advanceLaunch(trig_, p);
        checkLaunchEnd(p, kmh);
        break;
      }

      case LState::Running:
        advanceLaunch(last_, p);
        checkLaunchEnd(p, kmh);
        break;
    }
  }

  void checkLaunchEnd(const Pt& p, double kmh) {
    if (st_ != LState::Running) return;
    if (kmh < STOP_KMH || kmh <= L_.peakKmh - LIFT_DROP_KMH || p.t - L_.lastPeakT >= PLATEAU_S) finishLaunch(false);
  }

  void advanceLaunch(const Pt& a, const Pt& b) {
    const double dt = b.t - a.t;
    if (dt <= 0) return;
    const double acc = (b.v - a.v) / dt;
    const double d0 = L_.dist;
    const double d1 = d0 + 0.5 * (a.v + b.v) * dt;
    auto mark = [&](double tau) {  // a measurement was made tau seconds into this segment
      L_.endAlt = a.alt + (b.alt - a.alt) * tau / dt;
      L_.endDist = d0 + a.v * tau + 0.5 * acc * tau * tau;
    };

    if (!L_.zeroSet) {
      if (!L_.r.rollout) {
        L_.zeroSet = true;  // t0 stays at the movement start
      } else if (d0 < METRES_PER_FOOT && d1 >= METRES_PER_FOOT) {
        L_.t0 = a.t + solveTau(a.v, acc, METRES_PER_FOOT - d0);
        L_.zeroSet = true;
      }
    }

    if (L_.zeroSet) {
      struct Milestone {
        double thr;
        float* field;
      };
      Milestone ms[] = {{60 * MS, &L_.r.t0to60},
                        {100 * MS, &L_.r.t0to100},
                        {60 * KMH_PER_MPH * MS, &L_.r.t0to60mph},
                        {200 * MS, nullptr}};
      for (const Milestone& m : ms) {
        if (!(a.v < m.thr && b.v >= m.thr)) continue;
        double tau = dt * (m.thr - a.v) / (b.v - a.v);
        double tc = a.t + tau;
        if (m.field) {
          if (*m.field >= 0) continue;
          *m.field = (float)(tc - L_.t0);
          if (m.thr == 100 * MS) L_.t100 = tc;
        } else if (L_.t100 >= 0 && L_.r.t100to200 < 0) {
          L_.r.t100to200 = (float)(tc - L_.t100);
        } else {
          continue;
        }
        mark(tau);
      }

      struct Mark {
        double dist;
        float *et, *trap;
      };
      Mark marks[] = {{EIGHTH_MILE_M, &L_.r.et8, &L_.r.trap8Kmh}, {QUARTER_MILE_M, &L_.r.et4, &L_.r.trap4Kmh}};
      for (const Mark& m : marks) {
        if (*m.et >= 0 || !(d0 < m.dist && d1 >= m.dist)) continue;
        double tau = solveTau(a.v, acc, m.dist - d0);
        *m.et = (float)(a.t + tau - L_.t0);
        *m.trap = (float)((a.v + acc * tau) * 3.6);
        mark(tau);
      }
    }

    L_.dist = d1;
    if (b.v * 3.6 > L_.peakKmh + PEAK_STEP_KMH) {
      L_.peakKmh = b.v * 3.6;
      L_.lastPeakT = b.t;
    }
  }

  void finishLaunch(bool gapped) {
    PerfResult r = L_.r;
    st_ = LState::Waiting;
    r.valid = !gapped;
    r.peakKmh = (float)L_.peakKmh;
    if (!gapped && !hasMeasurement(r)) return;               // never got going
    if (gapped && L_.peakKmh < MIN_INVALID_PEAK_KMH) return;  // not worth reporting
    r.startGpsMs = L_.zeroSet ? gpsMsAt(L_.t0) : 0;
    setLevel(r, L_.endAlt - L_.startAlt, L_.endDist);
    push(r);
  }

  // ---- 100-200 km/h from a rolling start --------------------------------------

  void stepRoll(const Pt& p) {
    const double from = ROLL_FROM_KMH * MS, to = ROLL_TO_KMH * MS;
    const double dt = p.t - last_.t;
    if (rollActive_) {
      rollDist_ += 0.5 * (last_.v + p.v) * dt;
      if (p.v < from) {
        rollActive_ = false;  // fell back below 100: not a clean pull
      } else if (p.v >= to) {
        double tau = dt * (to - last_.v) / (p.v - last_.v);
        PerfResult r;
        r.kind = RunKind::Roll;
        r.t100to200 = (float)(last_.t + tau - rollT100_);
        r.peakKmh = (float)(p.v * 3.6);
        r.startGpsMs = gpsMsAt(rollT100_);
        setLevel(r, p.alt - rollAlt0_, rollDist_);
        push(r);
        rollActive_ = false;
      }
    } else if (st_ != LState::Starting && st_ != LState::Running && last_.v < from && p.v >= from) {
      double tau = dt * (from - last_.v) / (p.v - last_.v);
      rollActive_ = true;
      rollT100_ = last_.t + tau;
      rollAlt0_ = last_.alt + (p.alt - last_.alt) * tau / dt;
      rollDist_ = 0.5 * (from + p.v) * (dt - tau);
    }
  }

  // ---- 100-0 km/h braking ------------------------------------------------------

  void stepBrake(const Pt& p) {
    const double from = BRAKE_FROM_KMH * MS;
    const double dt = p.t - last_.t;
    if (!brakeActive_) {
      if (!(last_.v >= from && p.v < from)) return;
      double tau = dt * (last_.v - from) / (last_.v - p.v);
      brakeActive_ = true;
      brakeT0_ = last_.t + tau;
      brakeAlt0_ = last_.alt + (p.alt - last_.alt) * tau / dt;
      brakeDist_ = 0.5 * (from + p.v) * (dt - tau);
      brakeMinV_ = p.v;
      brakePrev_ = last_;
      return;
    }

    if (p.v * 3.6 < STOP_KMH) {
      // The stop fell somewhere between the last fix and this one. Carry on from the last
      // moving fix at the rate speed was falling just before it.
      double decel = (brakePrev_.v - last_.v) / (last_.t - brakePrev_.t);
      double extraS = 0, extraM = 0;
      if (decel > 0.5) {
        extraS = last_.v / decel;
        if (extraS > 2 * dt) extraS = 2 * dt;
        extraM = last_.v * extraS - 0.5 * decel * extraS * extraS;
      }
      PerfResult r;
      r.kind = RunKind::Brake;
      r.brakeM = (float)(brakeDist_ + extraM);
      r.brakeS = (float)(last_.t + extraS - brakeT0_);
      r.peakKmh = BRAKE_FROM_KMH;
      r.startGpsMs = gpsMsAt(brakeT0_);
      setLevel(r, p.alt - brakeAlt0_, r.brakeM);
      push(r);
      brakeActive_ = false;
      return;
    }

    brakeDist_ += 0.5 * (last_.v + p.v) * dt;
    if (p.v > brakeMinV_ + BRAKE_REBOUND_KMH * MS) {
      brakeActive_ = false;  // speeding up again: not a stop
      return;
    }
    if (p.v < brakeMinV_) brakeMinV_ = p.v;
    brakePrev_ = last_;
  }
};

}  // namespace perf

#endif  // PERF_RUN_H
