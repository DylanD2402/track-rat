/**
 * Track Rat - What the battery has been through (no Arduino code, so it is tested on the PC)
 *
 * The board measures only the battery voltage. From that, once a minute, this works out how fast each way of
 * running uses the battery, how long it has left, how many charge cycles it has had, how healthy it is (the
 * jump the charger gives the reading gets bigger as the cell's resistance grows), and what voltage a full
 * charge really reads. All of it is an estimate that improves with use. StatsData is what is saved to flash.
 */

#ifndef BATTERY_STATS_H
#define BATTERY_STATS_H

#include <stdint.h>
#include <string.h>
#include "Battery.h"

namespace battery {

enum class Mode : uint8_t { ScreenOn, ScreenOnWifi, ScreenOff, Asleep };
constexpr int MODE_COUNT = 4;
inline int modeIndex(Mode m) { return (int)m; }

enum class Health : uint8_t { Learning, Good, Fair, Replace };

inline const char* modeName(Mode m) {
  switch (m) {
    case Mode::ScreenOn: return "Screen on";
    case Mode::ScreenOnWifi: return "Screen on, Wi-Fi";
    case Mode::ScreenOff: return "Screen off";
    default: return "Asleep";
  }
}
inline const char* healthName(Health h) {
  switch (h) {
    case Health::Good: return "Good";
    case Health::Fair: return "Fair";
    case Health::Replace: return "Replace";
    default: return "Learning";
  }
}

// How the numbers below are measured
constexpr int MIN_VALID_MV = 2500, MAX_VALID_MV = 4300;  // a minute mean outside this is a glitch
constexpr int RELAX_MINUTES = 5;                         // after charging the cell relaxes: that looks like fast drain
constexpr int RISE_IGNORE_MV = 3;                        // a minute that rose more than this was a recovery, not drain
constexpr int MAX_MINUTE_DROP_X100 = 500;                // 5 % in a minute is not drain
constexpr int FLOAT_MV = 4175;                           // at or above this a finished charger may be holding the cell up
constexpr uint32_t MIN_MEASURE_SECS = 30 * 60;           // a mode needs this much time ...
constexpr int MIN_MEASURE_DROP_X100 = 200;               // ... and this much drop (2 %) before its rate is believed
constexpr int SLEEP_JUMP_MV = 10;                        // waking this far above where it went to sleep = it was charged
constexpr int FULL_FLAT_MIN = 10, FULL_FLAT_MV = 2;      // charging minute means flat to within 2 mV over 10 minutes
constexpr int FULL_WAIT_MIN = 5;                         // the resting full voltage is read this long after
constexpr int FULL_SETTLE_MV = 3, FULL_RETRIES = 3;      // and must have settled (moved less than this in a minute)
constexpr int FULL_SLOW_FALL_MV = 25;                    // charging cleared by a fall under this = the charger finished
constexpr int STEP_SAMPLES = 5;                          // plug-in steps for the baseline and for the latest
constexpr int STEP_MAX_FROM_MV = 4100;                   // a step only counts below this (constant current phase)
constexpr int STEP_MIN_MV = 20, STEP_MAX_MV = 300;
constexpr int RUN_START_PERCENT = 95;                    // a full run starts unplugged at this much or more
constexpr uint32_t RUN_MIN_SECS = 3600;                  // shorter than this is not a run
constexpr int CYCLES_GOOD = 300, CYCLES_FAIR = 500;
constexpr int RISE_GOOD_X100 = 130, RISE_FAIR_X100 = 160;

// What the glue gives the maths: the pack's capacity and, per mode, the draw to assume until it is measured
struct Defaults {
  int capacityMah;
  int milliamps[MODE_COUNT];
};

// Saved to flash. No padding anywhere, so the check sums every byte. Anything wrong with it and it is thrown
// away: "not measured yet" is always better than nonsense.
struct StatsData {
  uint32_t magic;
  uint32_t secs[MODE_COUNT];       // time measured in each mode
  int32_t dropX100[MODE_COUNT];    // percent used in each mode, x100 (can dip below 0 by noise: reported as 0)
  int32_t totalDropX100;           // all modes: the charge cycles
  uint32_t runSecs[MODE_COUNT];    // the run in progress (unplugged full, not charged since)
  uint32_t lastRunMin, bestRunMin; // finished runs, in minutes (0 = none)
  int16_t fullMv;                  // resting voltage of a full charge, 0 = not learned
  int16_t stepFirst[STEP_SAMPLES]; // the first plug-in steps (the baseline)
  int16_t stepRecent[STEP_SAMPLES];// the latest ones (a ring)
  int16_t stepsTotal;              // how many steps have been taken (stops at 30000)
  uint8_t stepFirstCount;
  uint8_t stepRecentNext;
  uint8_t runActive;
  uint8_t reserved;
  uint8_t lastMix[MODE_COUNT];     // percent of each finished run spent in each mode
  uint8_t bestMix[MODE_COUNT];
  uint32_t check;

  static constexpr uint32_t MAGIC = 0x42415401;  // "BAT" + version 1
  void reset() {
    memset(this, 0, sizeof(*this));
    magic = MAGIC;
    check = sum();
  }
  uint32_t sum() const {  // FNV-1a over everything before `check`
    const uint8_t* p = (const uint8_t*)this;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(*this) - sizeof(check); i++) h = (h ^ p[i]) * 16777619u;
    return h;
  }
  void seal() { check = sum(); }
  static bool inRange(int32_t v, int32_t lo, int32_t hi) { return v >= lo && v <= hi; }
  bool valid() const {
    if (magic != MAGIC || check != sum()) return false;
    const uint32_t MAX_SECS = 1000000000u;
    for (int i = 0; i < MODE_COUNT; i++) {
      if (secs[i] > MAX_SECS || runSecs[i] > MAX_SECS) return false;
      if (!inRange(dropX100[i], -100000, 1000000000)) return false;
      // more than 5 % a minute never happened
      if (dropX100[i] > (int64_t)(secs[i] / 60 + 1) * MAX_MINUTE_DROP_X100) return false;
      if (lastMix[i] > 100 || bestMix[i] > 100) return false;
    }
    if (!inRange(totalDropX100, -100000, 2000000000)) return false;
    if (lastRunMin > 525600 || bestRunMin > 525600 || runActive > 1) return false;
    if (fullMv != 0 && !fullMvValid(fullMv)) return false;
    if (stepsTotal < 0 || stepsTotal > 30000 || stepRecentNext >= STEP_SAMPLES) return false;
    if (stepFirstCount != (stepsTotal < STEP_SAMPLES ? stepsTotal : STEP_SAMPLES)) return false;
    for (int i = 0; i < stepFirstCount; i++)
      if (!inRange(stepFirst[i], STEP_MIN_MV, STEP_MAX_MV)) return false;
    const int recent = stepsTotal < STEP_SAMPLES ? stepsTotal : STEP_SAMPLES;
    for (int i = 0; i < recent; i++)
      if (!inRange(stepRecent[i], STEP_MIN_MV, STEP_MAX_MV)) return false;
    return true;
  }
};
static_assert(sizeof(StatsData) == 104, "StatsData must have no padding (the check sums every byte)");

// One mode's rate
struct ModeUsage {
  bool measured;      // false: the default draw, an estimate
  float pctPerHour;
  float milliamps;
};

// Everything the screens and the phone page show
struct Report {
  bool present = false;     // the glue fills these four in
  int percent = -1;
  int mv = 0;
  bool charging = false;
  int capacityMah = 0;
  Mode mode = Mode::ScreenOn;  // how it is running now
  ModeUsage usage[MODE_COUNT] = {};
  bool haveTimeLeft = false;   // not while charging or with no battery
  float hoursLeft = 0;
  bool timeLeftEstimate = false;  // the default draw was used
  float runHours = 0;             // a full charge lasts about this long
  bool runEstimate = true;        // ... from capacity and the draw, not from a full run
  float lastRunHours = 0, bestRunHours = 0;  // 0 = none yet
  uint8_t lastMix[MODE_COUNT] = {}, bestMix[MODE_COUNT] = {};
  float cycles = 0;
  Health health = Health::Learning;
  int steps = 0;               // plug-in steps taken so far
  int stepNowMv = 0, stepFirstMv = 0;  // 0 until there are STEP_SAMPLES
  int fullMv = 0;              // 0 = not learned
};

inline int median5(const int16_t* v) {
  int16_t a[STEP_SAMPLES];
  for (int i = 0; i < STEP_SAMPLES; i++) a[i] = v[i];
  for (int i = 1; i < STEP_SAMPLES; i++)
    for (int j = i; j > 0 && a[j - 1] > a[j]; j--) {
      const int16_t t = a[j];
      a[j] = a[j - 1];
      a[j - 1] = t;
    }
  return a[STEP_SAMPLES / 2];
}

// Seconds come in one at a time; every 60th gives the minute: its mean voltage, the mode it mostly was in, and
// whether the charger was in at any point (a minute with a plug-in or unplug in it is no use for drain).
class MinuteMeter {
public:
  bool add(int mv, Mode mode, bool charging) {
    sum += mv;
    modeSecs[modeIndex(mode)]++;
    if (charging) chargeSecs++;
    if (++count < 60) return false;
    doneMv = (int)((sum + 30) / 60);
    doneCharging = chargeSecs > 0;
    int best = 0;
    for (int i = 1; i < MODE_COUNT; i++)
      if (modeSecs[i] > modeSecs[best]) best = i;
    doneMode = (Mode)best;
    sum = 0;
    count = chargeSecs = 0;
    for (int i = 0; i < MODE_COUNT; i++) modeSecs[i] = 0;
    return true;
  }
  void reset() {
    sum = 0;
    count = chargeSecs = 0;
    for (int i = 0; i < MODE_COUNT; i++) modeSecs[i] = 0;
  }
  int meanMv() const { return doneMv; }
  Mode mode() const { return doneMode; }
  bool charging() const { return doneCharging; }

private:
  int32_t sum = 0;
  int count = 0, chargeSecs = 0;
  int modeSecs[MODE_COUNT] = {};
  int doneMv = 0;
  Mode doneMode = Mode::ScreenOn;
  bool doneCharging = false;
};

class Stats {
public:
  explicit Stats(const Defaults& d) : def(d) { data_.reset(); }

  // The saved data. Corrupt, zeroed or from another version: start again from nothing.
  void load(const StatsData& in) {
    data_ = in;
    if (!data_.valid()) data_.reset();
    changed = false;
  }
  const StatsData& data() const { return data_; }
  bool dirty() const { return changed; }  // changed since the last save
  void saved() { changed = false; }
  int fullMv() const { return data_.fullMv; }
  // True once after a full voltage was learned (the charger finished: the cell is full)
  bool takeFull() {
    const bool e = fullEvent;
    fullEvent = false;
    return e;
  }

  // Once a minute: the mode, the minute-mean voltage, whether the charger was in at any point, and the shown
  // percent (for starting a full run).
  void addMinute(Mode mode, int meanMv, bool charging, int shownPercent) {
    if (meanMv < MIN_VALID_MV || meanMv > MAX_VALID_MV || mode == Mode::Asleep) {
      havePrev = false;
      return;
    }
    const int m = modeIndex(mode);
    if (charging) {
      endRun();
      tickFull(meanMv);
      for (int i = FULL_FLAT_MIN - 1; i > 0; i--) chargeMeans[i] = chargeMeans[i - 1];
      chargeMeans[0] = meanMv;
      if (chargeCount < FULL_FLAT_MIN) chargeCount++;
      if (!fullLatch && fullWait == 0 && chargeCount >= FULL_FLAT_MIN && meanMv >= FULL_MIN_MV && flat()) startFullWait(meanMv);
      wasCharging = true;
      relax = 0;
      havePrev = false;
      return;
    }
    tickFull(meanMv);
    if (wasCharging) {  // unplugged, or the charger finished
      wasCharging = false;
      const int before = chargeCount >= 2 ? chargeMeans[1] : chargeMeans[0];  // (the newest may hold the step itself)
      if (!fullLatch && fullWait == 0 && chargeCount > 0 && before >= FULL_MIN_MV && before - meanMv < FULL_SLOW_FALL_MV)
        startFullWait(meanMv);
      if (shownPercent >= RUN_START_PERCENT && !data_.runActive) {
        memset(data_.runSecs, 0, sizeof(data_.runSecs));
        data_.runActive = 1;
        touch();
      }
      chargeCount = 0;
    }
    if (meanMv < FULL_MIN_MV) fullLatch = false;  // the next charge is a new one
    if (meanMv >= FLOAT_MV) {  // a finished charger may be holding the cell up: not drain, not a run
      havePrev = false;
      return;
    }
    if (data_.runActive) {
      data_.runSecs[m] += 60;
      touch();
    }
    if (relax < RELAX_MINUTES) {
      relax++;
      havePrev = false;
      return;
    }
    if (havePrev && prevMode == mode) {
      const int drop = percentFromMvX100(prevMv) - percentFromMvX100(meanMv);
      if (meanMv <= prevMv + RISE_IGNORE_MV && drop <= MAX_MINUTE_DROP_X100) {
        data_.secs[m] += 60;
        data_.dropX100[m] += drop;
        data_.totalDropX100 += drop;
        touch();
      }
    }
    prevMv = meanMv;
    prevMode = mode;
    havePrev = true;
  }

  // A plug-in step the Monitor saw (takeStep): its size and the voltage just before it
  void addStep(int stepMv, int fromMv) {
    if (fromMv >= STEP_MAX_FROM_MV || fromMv < MIN_VALID_MV || stepMv < STEP_MIN_MV || stepMv > STEP_MAX_MV) return;
    if (data_.stepFirstCount < STEP_SAMPLES) data_.stepFirst[data_.stepFirstCount++] = (int16_t)stepMv;
    data_.stepRecent[data_.stepRecentNext] = (int16_t)stepMv;
    data_.stepRecentNext = (data_.stepRecentNext + 1) % STEP_SAMPLES;
    if (data_.stepsTotal < 30000) data_.stepsTotal++;
    touch();
  }

  // Waking from deep sleep: how long it slept and the voltage when it went to sleep and now. A voltage that
  // jumped means it was charged meanwhile: nothing is learned from that.
  void addSleep(uint32_t secs, int startMv, int wakeMv) {
    if (secs == 0 || startMv < MIN_VALID_MV || startMv > MAX_VALID_MV || wakeMv < MIN_VALID_MV || wakeMv > MAX_VALID_MV) return;
    wasCharging = false;
    havePrev = false;
    if (wakeMv > startMv + SLEEP_JUMP_MV) {
      endRun();
      return;
    }
    if (secs > 100u * 86400u) secs = 100u * 86400u;
    if (data_.runActive) data_.runSecs[modeIndex(Mode::Asleep)] += secs;
    if (secs >= MIN_MEASURE_SECS) {
      int drop = percentFromMvX100(startMv) - percentFromMvX100(wakeMv);
      const int cap = (int)(secs / 60 > 20000 ? 20000 : secs / 60) * MAX_MINUTE_DROP_X100;
      if (drop > cap) drop = cap;
      if (drop > 0) {
        data_.secs[modeIndex(Mode::Asleep)] += secs;
        data_.dropX100[modeIndex(Mode::Asleep)] += drop;
        data_.totalDropX100 += drop;
      }
    }
    touch();
  }

  // The battery has run down to the low cutoff: if it was a whole run from full, keep it
  void lowReached() {
    if (!data_.runActive) return;
    data_.runActive = 0;
    uint64_t total = 0;
    for (int i = 0; i < MODE_COUNT; i++) total += data_.runSecs[i];
    if (total >= RUN_MIN_SECS && total / 60 <= 525600) {
      const uint32_t minutes = (uint32_t)(total / 60);
      data_.lastRunMin = minutes;
      for (int i = 0; i < MODE_COUNT; i++) data_.lastMix[i] = (uint8_t)((data_.runSecs[i] * 100ull + total / 2) / total);
      if (minutes > data_.bestRunMin) {
        data_.bestRunMin = minutes;
        for (int i = 0; i < MODE_COUNT; i++) data_.bestMix[i] = data_.lastMix[i];
      }
    }
    memset(data_.runSecs, 0, sizeof(data_.runSecs));
    touch();
  }

  bool measured(Mode m) const {
    const int i = modeIndex(m);
    return data_.secs[i] >= MIN_MEASURE_SECS && data_.dropX100[i] >= MIN_MEASURE_DROP_X100;
  }

  // current: how it is running now. charging / shownPercent: the Monitor's (shownPercent -1 = no battery).
  Report report(Mode current, int shownPercent, bool charging) const {
    Report r;
    r.capacityMah = def.capacityMah;
    r.mode = current;
    for (int i = 0; i < MODE_COUNT; i++) {
      ModeUsage& u = r.usage[i];
      u.measured = measured((Mode)i);
      if (u.measured) {
        u.pctPerHour = data_.dropX100[i] / 100.0f * 3600.0f / data_.secs[i];
        u.milliamps = u.pctPerHour / 100.0f * def.capacityMah;
      } else {
        u.milliamps = (float)def.milliamps[i];
        u.pctPerHour = def.capacityMah > 0 ? u.milliamps * 100.0f / def.capacityMah : 0;
      }
    }
    const ModeUsage& now = r.usage[modeIndex(current)];
    if (shownPercent >= 0 && !charging && now.pctPerHour > 0) {
      r.haveTimeLeft = true;
      r.hoursLeft = shownPercent / now.pctPerHour;
      if (r.hoursLeft > 9999) r.hoursLeft = 9999;
      r.timeLeftEstimate = !now.measured;
    }
    const ModeUsage& on = r.usage[modeIndex(Mode::ScreenOn)];
    r.runHours = on.milliamps > 0 ? def.capacityMah / on.milliamps : 0;
    r.runEstimate = true;
    if (data_.lastRunMin) {
      r.runHours = data_.lastRunMin / 60.0f;
      r.runEstimate = false;
      r.lastRunHours = r.runHours;
    }
    r.bestRunHours = data_.bestRunMin / 60.0f;
    for (int i = 0; i < MODE_COUNT; i++) {
      r.lastMix[i] = data_.lastMix[i];
      r.bestMix[i] = data_.bestMix[i];
    }
    r.cycles = data_.totalDropX100 > 0 ? data_.totalDropX100 / 10000.0f : 0;
    r.steps = data_.stepsTotal;
    r.fullMv = data_.fullMv;
    if (data_.stepsTotal >= STEP_SAMPLES) {
      r.stepFirstMv = median5(data_.stepFirst);
      r.stepNowMv = median5(data_.stepRecent);
      const int rise = r.stepNowMv * 100 / r.stepFirstMv;  // x100
      if (r.cycles < CYCLES_GOOD && rise < RISE_GOOD_X100) r.health = Health::Good;
      else if (r.cycles < CYCLES_FAIR && rise < RISE_FAIR_X100) r.health = Health::Fair;
      else r.health = Health::Replace;
    }
    return r;
  }

private:
  void touch() {
    data_.seal();
    changed = true;
  }
  void endRun() {
    if (!data_.runActive) return;
    data_.runActive = 0;
    memset(data_.runSecs, 0, sizeof(data_.runSecs));
    touch();
  }
  bool flat() const {
    int lo = chargeMeans[0], hi = chargeMeans[0];
    for (int i = 1; i < FULL_FLAT_MIN; i++) {
      if (chargeMeans[i] < lo) lo = chargeMeans[i];
      if (chargeMeans[i] > hi) hi = chargeMeans[i];
    }
    return hi - lo <= FULL_FLAT_MV;
  }
  void startFullWait(int meanMv) {
    fullWait = FULL_WAIT_MIN;
    fullRetries = FULL_RETRIES;
    fullPrev = meanMv;
  }
  // The charger finished a while ago: read the resting voltage if it has settled
  void tickFull(int meanMv) {
    if (fullWait == 0) return;
    if (!fullMvValid(meanMv)) {  // dropped away (really on battery, or unplugged): give up
      fullWait = 0;
      return;
    }
    const int moved = meanMv > fullPrev ? meanMv - fullPrev : fullPrev - meanMv;
    fullPrev = meanMv;
    if (--fullWait > 0) return;
    if (moved > FULL_SETTLE_MV) {
      if (fullRetries-- > 0) fullWait = 1;
      return;
    }
    data_.fullMv = (int16_t)(data_.fullMv ? (data_.fullMv * 3 + meanMv + 2) / 4 : meanMv);
    fullLatch = true;
    fullEvent = true;
    touch();
  }

  Defaults def;
  StatsData data_;
  bool changed = false;
  bool fullEvent = false;
  // Running state: lost with the RAM, which only costs a few minutes of readings
  bool wasCharging = false;
  int relax = 0;
  bool havePrev = false;
  int prevMv = 0;
  Mode prevMode = Mode::ScreenOn;
  int chargeMeans[FULL_FLAT_MIN] = {};  // charging minute means, newest first
  int chargeCount = 0;
  int fullWait = 0, fullRetries = 0, fullPrev = 0;
  bool fullLatch = false;  // one full voltage per charge
};

}  // namespace battery

#endif  // BATTERY_STATS_H
