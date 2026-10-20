/**
 * Track Rat - lap tags
 *
 * Every lap gets tags that say whether it can be trusted as a reference:
 *
 * - Pit lap: the car stopped (under PIT_STOP_KMH) during the lap.
 * - Slow lap: more than SLOW_LAP_PERCENT of the best clean lap (cool-down,
 *   traffic, yellow flags).
 * - Suspect lap: a gap between fixes over SUSPECT_GAP_MS, a fix whose
 *   accuracy was worse than SUSPECT_ACCURACY_M (when the GPS reports it), or
 *   a lap distance more than SUSPECT_DISTANCE_PERCENT off the session's
 *   median (once there are SUSPECT_DISTANCE_MIN_LAPS laps to take it from).
 *
 * A tagged lap is never the best lap or the live-delta reference, and is
 * left out of averages. Pit and suspect tags on a single fix are known when
 * the fix arrives (fixFlags); slow and distance tags depend on the whole
 * session, so retag() works them out again after every lap.
 *
 * The same bits mark individual fixes in the .vbo log's "Tag" channel: the
 * fix where the car was stopped, the first fix after a gap, a fix with poor
 * accuracy. So a lap's tags include every flag its fixes carry, and a
 * session timed again from its log gets the same tags.
 *
 * No Arduino dependencies: unit-tested on a PC in test/test_sessions.cpp.
 */

#ifndef LAP_TAGS_H
#define LAP_TAGS_H

#include <stdint.h>
#include <stddef.h>
#include <algorithm>
#include <string>
#include <vector>
#include "Config.h"

namespace laptags {

enum : uint8_t {
  PIT = 1,            // stopped during the lap
  SLOW = 2,           // over SLOW_LAP_PERCENT of the best clean lap
  GPS_GAP = 4,        // suspect: a gap between fixes
  POOR_ACCURACY = 8,  // suspect: a fix less accurate than SUSPECT_ACCURACY_M
  ODD_DISTANCE = 16,  // suspect: the lap's length is off the usual
};
constexpr uint8_t SUSPECT = GPS_GAP | POOR_ACCURACY | ODD_DISTANCE;
constexpr uint8_t FROM_FIXES = PIT | GPS_GAP | POOR_ACCURACY;  // the rest come from comparing laps

// Can this lap be the best lap or the live-delta reference? (Slow laps never
// are anyway: they're slower than the best.)
inline bool clean(uint8_t tags) { return (tags & (PIT | SUSPECT)) == 0; }
inline bool suspect(uint8_t tags) { return (tags & SUSPECT) != 0; }

// The flags one fix gives its lap. hAccM < 0: the GPS doesn't say (NMEA).
// gapMs: time since the previous fix (0 for the first).
inline uint8_t fixFlags(float speedKmh, float hAccM, uint32_t gapMs) {
  uint8_t f = 0;
  if (speedKmh < PIT_STOP_KMH) f |= PIT;
  if (gapMs > SUSPECT_GAP_MS) f |= GPS_GAP;
  if (hAccM > SUSPECT_ACCURACY_M) f |= POOR_ACCURACY;
  return f;
}

// One lap as retag() needs it.
struct LapInfo {
  uint32_t timeMs;
  float distanceM;
  uint8_t tags;  // in: at least the FROM_FIXES flags; out: all of them
};

// Works out the slow and distance tags again for every lap (the fix flags
// stay as they are). Returns the index of the best clean lap, or -1.
inline int retag(std::vector<LapInfo>& laps) {
  for (LapInfo& l : laps) l.tags &= FROM_FIXES;

  if (laps.size() >= SUSPECT_DISTANCE_MIN_LAPS) {
    std::vector<float> d;
    for (const LapInfo& l : laps) d.push_back(l.distanceM);
    std::sort(d.begin(), d.end());
    const size_t n = d.size();
    const float median = n % 2 ? d[n / 2] : (d[n / 2 - 1] + d[n / 2]) / 2;
    const float allowed = median * SUSPECT_DISTANCE_PERCENT / 100.0f;
    for (LapInfo& l : laps) {
      float off = l.distanceM - median;
      if (off < 0) off = -off;
      if (off > allowed) l.tags |= ODD_DISTANCE;
    }
  }

  int best = -1;
  for (size_t i = 0; i < laps.size(); i++)
    if (clean(laps[i].tags) && (best < 0 || laps[i].timeMs < laps[best].timeMs)) best = (int)i;
  if (best >= 0) {
    const uint64_t limit = (uint64_t)laps[best].timeMs * SLOW_LAP_PERCENT;
    for (LapInfo& l : laps)
      if ((uint64_t)l.timeMs * 100 > limit) l.tags |= SLOW;
  }
  return best;
}

// For the summary JSON and the screens: "pit", "slow", "suspect".
inline std::vector<const char*> names(uint8_t tags) {
  std::vector<const char*> out;
  if (tags & PIT) out.push_back("pit");
  if (tags & SLOW) out.push_back("slow");
  if (tags & SUSPECT) out.push_back("suspect");
  return out;
}
// Why a lap is suspect: "gps gap", "accuracy", "distance".
inline std::vector<const char*> suspectReasons(uint8_t tags) {
  std::vector<const char*> out;
  if (tags & GPS_GAP) out.push_back("gps gap");
  if (tags & POOR_ACCURACY) out.push_back("accuracy");
  if (tags & ODD_DISTANCE) out.push_back("distance");
  return out;
}
// One short word for a lap list row, the most serious first ("" if clean and not slow).
inline const char* shortLabel(uint8_t tags) {
  if (tags & SUSPECT) return "SUSPECT";
  if (tags & PIT) return "PIT";
  if (tags & SLOW) return "SLOW";
  return "";
}

}  // namespace laptags

#endif  // LAP_TAGS_H
