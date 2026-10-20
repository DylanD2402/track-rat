/**
 * Track Rat - the screen follows the situation
 *
 * Which screen the timing session shows, without a touch (ROADMAP 2.5):
 *
 * - Fast: the timing screen.
 * - Slow: under SLOW_SCREEN_KMH for SLOW_SCREEN_MS, or in the pit lane when
 *   the track's map marks it ("pit" in tracks.json): big speed, last lap and
 *   the session clock. Back to timing as soon as the car is quicker again
 *   (and out of the pit lane). The threshold sits below the slowest hairpins.
 * - Parked: the debrief (PitWatch in AutoStart.h decides that).
 *
 * Plus the session clock in the header: time since the session started, or
 * the time left when a session length is set.
 *
 * Time is the fixes' own GPS time (ms since UTC midnight), so it doesn't
 * drift with the loop and works on a replay. No Arduino dependencies:
 * tested in test/test_situation.cpp. C++11, like the firmware.
 */

#ifndef SITUATION_SCREEN_H
#define SITUATION_SCREEN_H

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "Config.h"
#include "LayoutMatch.h"

namespace situation {

// Is (x, y) in the pit lane: within PIT_LANE_M of a line marked as the pit
// lane, and no nearer any other line (the main straight often runs beside it)?
inline bool inPitLane(const layout::Lines& map, const std::vector<uint16_t>& pitLines, float x, float y) {
  if (pitLines.empty() || map.empty()) return false;
  size_t line = 0;
  const float d2 = layout::nearestD2(map, x, y, &line);
  if (d2 > (float)(PIT_LANE_M * PIT_LANE_M)) return false;
  for (uint16_t p : pitLines)
    if (p == line) return true;
  return false;
}

// Slow or not, one fix at a time.
class SlowWatch {
public:
  void reset() {
    isSlow = false;
    slowSince = false;
  }
  // inPit: the fix is in the pit lane. Returns slow().
  bool onFix(float speedKmh, uint32_t gpsTimeMs, bool inPit) {
    if (inPit) {
      isSlow = true;
      slowSince = false;
      return isSlow;
    }
    if (speedKmh >= SLOW_SCREEN_KMH) {
      isSlow = false;
      slowSince = false;
      return isSlow;
    }
    if (!slowSince) {
      slowSince = true;
      since = gpsTimeMs;
    }
    if (elapsed(since, gpsTimeMs) >= SLOW_SCREEN_MS) isSlow = true;
    return isSlow;
  }
  bool slow() const { return isSlow; }

private:
  static constexpr uint32_t DAY_MS = 86400000UL;
  static uint32_t elapsed(uint32_t a, uint32_t b) { return (b + DAY_MS - a) % DAY_MS; }  // across midnight
  bool isSlow = false;
  bool slowSince = false;  // slower than SLOW_SCREEN_KMH since...
  uint32_t since = 0;
};

// The Session length setting's choices, in minutes (0: no countdown).
constexpr int SESSION_LENGTHS[] = {0, 20, 25, 30};
constexpr int SESSION_LENGTH_COUNT = 4;
inline int sessionLengthMin(uint8_t choice) { return choice < SESSION_LENGTH_COUNT ? SESSION_LENGTHS[choice] : 0; }

// "m:ss" (or "h:mm:ss") of a time in ms.
inline std::string clockText(uint32_t ms) {
  const uint32_t s = ms / 1000;
  char buf[16];
  if (s >= 3600) snprintf(buf, sizeof(buf), "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
  else snprintf(buf, sizeof(buf), "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
  return buf;
}

// The header's session clock: time since the session started, or with a
// session length the time left ("7:26 left", then "Time up"). Remaining time
// counts down in whole seconds rounded up, so it reads 0:01 until the end.
inline std::string sessionClock(uint32_t elapsedMs, int lengthMin) {
  if (lengthMin <= 0) return clockText(elapsedMs);
  const uint32_t len = (uint32_t)lengthMin * 60000UL;
  if (elapsedMs >= len) return "Time up";
  return clockText(len - elapsedMs + 999) + " left";
}

// Under a minute left (or time up): the clock turns yellow.
inline bool sessionEnding(uint32_t elapsedMs, int lengthMin) {
  return lengthMin > 0 && elapsedMs + 60000UL > (uint32_t)lengthMin * 60000UL;
}

}  // namespace situation

#endif  // SITUATION_SCREEN_H
