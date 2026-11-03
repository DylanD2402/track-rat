/**
 * Track Rat - Battery maths (no Arduino code, so it is tested on the PC)
 *
 * Feed it the battery voltage in millivolts about once a second. It smooths
 * the readings, says whether a battery is fitted, gives a percentage, and
 * says when the battery is low enough that the unit should save and sleep.
 */

#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

namespace battery {

// LiPo resting voltage -> percent, from a typical 3.7 V cell discharge curve.
// The board's charger stops at 4.2 V. Voltage sags under load and reads high
// while charging, so the percentage is a rough guide, not a fuel gauge.
struct CurvePoint {
  int mv;
  int percent;
};
static const CurvePoint CURVE[] = {{4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 75},
                                   {3950, 70},  {3910, 65}, {3870, 60}, {3850, 55}, {3840, 50}, {3820, 45},
                                   {3800, 40},  {3790, 35}, {3770, 30}, {3750, 25}, {3730, 20}, {3710, 15},
                                   {3690, 10},  {3610, 5},  {3300, 0}};
static const int CURVE_POINTS = sizeof(CURVE) / sizeof(CURVE[0]);

inline int percentFromMv(int mv) {
  if (mv >= CURVE[0].mv) return 100;
  if (mv <= CURVE[CURVE_POINTS - 1].mv) return 0;
  for (int i = 1; i < CURVE_POINTS; i++) {
    if (mv >= CURVE[i].mv) {
      const CurvePoint& hi = CURVE[i - 1];
      const CurvePoint& lo = CURVE[i];
      return lo.percent + (hi.percent - lo.percent) * (mv - lo.mv) / (hi.mv - lo.mv);
    }
  }
  return 0;
}

// Voltage at the ADC pin -> battery voltage. The board has two equal
// resistors (2 x 200 k) between BAT+ and ground, so the pin sees half.
inline int batteryMvFromPinMv(int pinMv, float dividerRatio, int offsetMv) {
  return (int)(pinMv * dividerRatio + 0.5f) + offsetMv;
}

struct Limits {
  int presentMinMv;     // below this there is no battery (pin floating or shorted to ground)
  int presentMaxMv;     // above this the charger is holding an empty socket at its float voltage
  int lowMv;            // at or below this the battery is nearly flat
  uint32_t lowHoldMs;   // ... for this long, so a bump or a load spike doesn't trigger it
  int hysteresisMv;     // margin before "no battery" clears again (stops the gauge flickering at the limit)
  int absentAfter;      // this many out-of-range readings in a row = no battery (one ADC glitch isn't)
  // Charging is worked out from the voltage alone (the board has no pin for it). The charger lifts the
  // reading by charge current x the cell's resistance, and unplugging drops it again.
  int chargeStepMv;     // a reading this far above/below the average, twice in a row = plugged in / unplugged
  int chargeOffsetMv;   // lift assumed while charging when no plug-in step was seen (boot, missed step)
  int chargeHoldMv;     // at or above this, only an unplug step clears "charging" (the charger may have finished)
  int trendRiseMv;      // up this much over 4 minutes (minute means) = charging
  int trendFallMv;      // down this much over 4 minutes = not charging
  int climbMinutes;     // slow climb: the minute means over this many minutes ...
  int climbRiseMv;      // ... rose by at least this much (the 4-minute rule above misses 0.3 mV a minute) ...
  int climbDipMv;       // ... with no minute more than this far below the one before ...
  int climbJumpMv;      // ... or more than this far above it (a recovery after a load drops is a jump, not a climb)
  uint32_t chargeStepMs;     // the shown percent rises by at most 1 in this time while charging
  uint32_t dischargeStepMs;  // ... and falls by at most 1 in this time otherwise
};

class Monitor {
public:
  explicit Monitor(const Limits& l) : lim(l) {}

  // mv: battery voltage, already averaged over a burst of ADC readings.
  void addSample(uint32_t nowMs, int mv) {
    // Present or not is judged on the raw reading; the average only smooths what is shown.
    bool inRange;
    if (fitted) inRange = mv >= lim.presentMinMv && mv <= lim.presentMaxMv;
    else inRange = mv >= lim.presentMinMv + lim.hysteresisMv && mv <= lim.presentMaxMv - lim.hysteresisMv;

    if (inRange) {
      outOfRange = 0;
    } else {
      if (fitted && ++outOfRange < lim.absentAfter) return;  // a lone bad reading changes nothing
      fitted = false;
      haveAverage = false;  // start again from a clean average if a battery appears
      lowSince = 0;
      lowTiming = false;
      resetCharge();
      return;
    }
    fitted = true;
    if (!haveAverage) {
      avg16 = mv * 16;
      haveAverage = true;
      refCount = 0;
      sum = 0;
      sumCount = 0;
      refAt = nowMs;
    } else {
      trackCharging(nowMs, mv);
      avg16 += (mv * 16 - avg16) / 8;  // 1 s samples: settles in about 10 s; x16 keeps the fraction
    }
    updateTrend(nowMs, mv);
    updateShown(nowMs);

    if (avg16 <= lim.lowMv * 16) {
      if (!lowTiming) {
        lowTiming = true;
        lowSince = nowMs;
      }
    } else {
      lowTiming = false;
    }
  }

  bool present() const { return fitted && haveAverage; }
  int millivolts() const { return present() ? (avg16 + 8) / 16 : 0; }
  // Shown percentage, iPhone style: the first reading is straight from the voltage; after that it only
  // rises while charging, only falls while not, and moves at most 1 % per chargeStepMs / dischargeStepMs.
  int percent() const { return present() ? shown : -1; }
  bool charging() const { return present() && charged(); }

  // A USB host (PC, car media port) is sending frames: the cable is in, whatever the voltage says. A plain wall
  // charger never sets this, so the voltage rules still do the work there. Call it before each sample.
  void setUsbHost(bool host) {
    if (host == usbHost) return;
    usbHost = host;
    if (!host) setCharging(false, 0);  // unplugged: the voltage rules must find charging again by themselves
  }

  // Low for long enough that the unit should save and sleep. Never true when no battery is fitted.
  bool low(uint32_t nowMs) const {
    return present() && lowTiming && (uint32_t)(nowMs - lowSince) >= lim.lowHoldMs;
  }

private:
  bool charged() const { return isCharging || usbHost; }

  void resetCharge() {
    isCharging = false;
    offsetMv = 0;
    up = down = 0;
    refCount = 0;
    sum = 0;
    sumCount = 0;
    haveShown = false;
    shown = 0;
  }

  void setCharging(bool on, int offset) {
    isCharging = on;
    offsetMv = offset < 0 ? 0 : offset > MAX_OFFSET_MV ? MAX_OFFSET_MV : offset;
    up = down = 0;
    refCount = 0;  // the step (or the trend that found it) must not count again
    sum = 0;
    sumCount = 0;
  }

  // Plug-in / unplug steps. Called before the average takes the new sample.
  void trackCharging(uint32_t nowMs, int mv) {
    const int dev16 = mv * 16 - avg16;
    if (!isCharging) {
      down = 0;
      if (dev16 >= lim.chargeStepMv * 16) {
        if (up++ == 0) stepBase16 = avg16;
        if (up >= 2) {
          setCharging(true, (mv * 16 - stepBase16 + 8) / 16);
          avg16 = mv * 16;  // snap: no 10 s crawl to the new level
          refAt = nowMs;
        }
      } else {
        up = 0;
      }
    } else {
      up = 0;
      if (dev16 <= -lim.chargeStepMv * 16) {
        if (++down >= 2) {
          setCharging(false, 0);
          avg16 = mv * 16;
          refAt = nowMs;
        }
      } else {
        down = 0;
      }
    }
  }

  // Slow fallback: the mean of each minute's readings, compared with the one from 4 minutes before (and see
  // climbing() for the slower rule). Near
  // full the charger lifts the cell only 1-2 mV a minute, so the window is long and the means keep ADC
  // noise (a few mV per reading) well under the thresholds.
  void updateTrend(uint32_t nowMs, int mv) {
    sum += mv;
    sumCount++;
    if ((uint32_t)(nowMs - refAt) < TREND_STEP_MS) return;
    refAt = nowMs;
    for (int i = MAX_REFS - 1; i > 0; i--) ref[i] = ref[i - 1];
    ref[0] = (int)(sum * 16 / sumCount);
    sum = 0;
    sumCount = 0;
    if (refCount < MAX_REFS) refCount++;
    if (refCount > TREND_SPAN) {
      const int change = (ref[0] - ref[TREND_SPAN]) / 16;
      if (!isCharging && change >= lim.trendRiseMv) {
        setCharging(true, lim.chargeOffsetMv);
      } else if (isCharging && change <= -lim.trendFallMv && avg16 < lim.chargeHoldMv * 16) {
        setCharging(false, 0);
      }
    }
    if (!isCharging && climbing()) setCharging(true, lim.chargeOffsetMv);
  }

  // A steady slow climb over climbMinutes: near full, on USB with no step to see, the cell gains only
  // 0.3 mV a minute. Every minute must be within climbDipMv of the one before (and no big jump up), so a
  // recovery after a load drops, which falls then jumps, is not taken for charging.
  bool climbing() const {
    const int span = lim.climbMinutes < MAX_REFS - 1 ? lim.climbMinutes : MAX_REFS - 1;
    if (span < 1 || refCount <= span) return false;
    if (ref[0] - ref[span] < lim.climbRiseMv * 16) return false;
    for (int i = 0; i < span; i++) {
      const int step = ref[i] - ref[i + 1];
      if (step < -lim.climbDipMv * 16 || step > lim.climbJumpMv * 16) return false;
    }
    return true;
  }

  // Best guess at the resting voltage. While charging the reading is lifted by about offsetMv; the lift
  // fades to nothing as the cell nears 4.2 V (the current has tailed off), so a full cell reads 100 %.
  int restingMv() const {
    const int mv = millivolts();
    if (!charged()) return mv;
    const int top = CURVE[0].mv, from = lim.chargeHoldMv - 50;
    int off = isCharging ? offsetMv : lim.chargeOffsetMv;  // (a USB host with no step seen: the usual lift)
    if (mv >= top) off = 0;
    else if (mv > from && top > from) off = off * (top - mv) / (top - from);
    return mv - off;
  }

  void updateShown(uint32_t nowMs) {
    const int target = percentFromMv(restingMv());
    if (!haveShown) {
      shown = target;
      haveShown = true;
      shownAt = nowMs;
      return;
    }
    const bool wants = charged() ? target > shown : target < shown;
    if (!wants) {
      shownAt = nowMs;  // the next step waits a full interval from when it is wanted
      return;
    }
    const uint32_t every = charged() ? lim.chargeStepMs : lim.dischargeStepMs;
    if ((uint32_t)(nowMs - shownAt) >= every) {
      shown += charged() ? 1 : -1;
      shownAt = nowMs;
    }
  }

  static const int MAX_OFFSET_MV = 250;
  static const uint32_t TREND_STEP_MS = 60000;
  static const int TREND_SPAN = 4;  // minutes the quick trend looks back
  static const int MAX_REFS = 16;   // minute means kept: the slow climb can look back up to 15 minutes

  Limits lim;
  bool fitted = false;
  int outOfRange = 0;
  bool haveAverage = false;
  int avg16 = 0;  // smoothed mV x 16
  bool lowTiming = false;
  uint32_t lowSince = 0;
  bool isCharging = false;
  int offsetMv = 0;  // size of the charger's lift
  int up = 0, down = 0;
  int stepBase16 = 0;
  bool usbHost = false;
  int ref[MAX_REFS] = {};  // minute means, newest first (x16)
  int refCount = 0;
  int32_t sum = 0;  // readings so far this minute
  int sumCount = 0;
  uint32_t refAt = 0;
  bool haveShown = false;
  int shown = 0;
  uint32_t shownAt = 0;
};

}  // namespace battery

#endif  // BATTERY_H
