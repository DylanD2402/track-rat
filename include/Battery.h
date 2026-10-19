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

    if (!inRange) {
      fitted = false;
      haveAverage = false;  // start again from a clean average if a battery appears
      lowSince = 0;
      lowTiming = false;
      return;
    }
    fitted = true;
    if (!haveAverage) {
      avgMv = mv;
      haveAverage = true;
    } else {
      avgMv += (mv - avgMv) / 4;  // 1 s samples: settles in a few seconds
    }

    if (avgMv <= lim.lowMv) {
      if (!lowTiming) {
        lowTiming = true;
        lowSince = nowMs;
      }
    } else {
      lowTiming = false;
    }
  }

  bool present() const { return fitted && haveAverage; }
  int millivolts() const { return present() ? avgMv : 0; }
  int percent() const { return present() ? percentFromMv(avgMv) : -1; }

  // Low for long enough that the unit should save and sleep. Never true when no battery is fitted.
  bool low(uint32_t nowMs) const {
    return present() && lowTiming && (uint32_t)(nowMs - lowSince) >= lim.lowHoldMs;
  }

private:
  Limits lim;
  bool fitted = false;
  bool haveAverage = false;
  int avgMv = 0;
  bool lowTiming = false;
  uint32_t lowSince = 0;
};

}  // namespace battery

#endif  // BATTERY_H
