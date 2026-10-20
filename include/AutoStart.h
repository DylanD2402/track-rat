/**
 * Track Rat - has the car driven off?
 *
 * At boot, a track within range is picked, but if the car is parked the board
 * waits on that track's Ready screen instead of starting a session (so the
 * menu is still reachable). DriveOffDetector says when to start: the speed
 * has stayed at AUTO_START_KMH or more for AUTO_START_HOLD_MS.
 *
 * Time is the fixes' own GPS time (ms since UTC midnight), so it doesn't
 * depend on how often the main loop runs, and it carries on across midnight.
 * A gap of more than MAX_FIX_GAP_MS between fixes, or a fix below the
 * threshold, starts the count again. Feed it real fixes only.
 *
 * Pure C++ (no Arduino), so it can be tested on a PC.
 */

#ifndef AUTO_START_H
#define AUTO_START_H

#include <stdint.h>
#include "Config.h"

class DriveOffDetector {
public:
  void reset() {
    fast = false;
    heldMs = 0;
  }

  // One fix. True once the car has been fast for long enough (and for every
  // fix after that, until reset()).
  bool onFix(float speedKmh, uint32_t gpsTimeMs) {
    if (speedKmh < AUTO_START_KMH) {
      reset();
      return false;
    }
    if (fast) {
      // Modulo a day, so a fix after midnight is a short step forward; a fix
      // from the past comes out as nearly a whole day and counts as a gap.
      const uint32_t dt = (gpsTimeMs + DAY_MS - lastMs) % DAY_MS;
      if (dt > MAX_FIX_GAP_MS) heldMs = 0;  // a dropout: the count starts again from here
      else heldMs += dt;
    } else {
      heldMs = 0;
    }
    fast = true;
    lastMs = gpsTimeMs;
    return heldMs >= AUTO_START_HOLD_MS;
  }

private:
  static constexpr uint32_t DAY_MS = 86400000UL;
  bool fast = false;       // the last fix was at or above the threshold
  uint32_t lastMs = 0;     // its GPS time
  uint32_t heldMs = 0;     // how long it has been fast
};

#endif  // AUTO_START_H
