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
 * PitWatch builds on it for automatic sessions: parked (under PIT_PARK_KMH)
 * for PIT_PARK_MS means the car is in the pits, so the summary shows. Driving
 * off (the same DriveOffDetector test) within SESSION_RESUME_MS of stopping
 * carries on with the same session (a red flag, a quick check); still parked
 * after that, the session is over, and the next drive off starts the next
 * one. nextSessionNumber numbers them through the day: "Session 2" is the
 * second at that track on that local date, also after a power cut.
 *
 * Pure C++ (no Arduino), so it can be tested on a PC (test_autostart.cpp,
 * test_sessions.cpp).
 */

#ifndef AUTO_START_H
#define AUTO_START_H

#include <stdint.h>
#include <string>
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

// Watches a running session for the car parking and driving off again.
class PitWatch {
public:
  enum class Event { None, Parked, MovedOff, SessionOver };

  void reset() {
    st = State::Driving;
    slow = false;
    off.reset();
  }

  // One fix (real ones only). Parked: show the summary. MovedOff: back to
  // timing, same session. SessionOver: parked too long, end the session.
  Event onFix(float speedKmh, uint32_t gpsTimeMs) {
    switch (st) {
      case State::Driving:
        if (speedKmh >= PIT_PARK_KMH) {
          slow = false;
          return Event::None;
        }
        if (!slow) {
          slow = true;
          stoppedMs = gpsTimeMs;
        }
        if (since(stoppedMs, gpsTimeMs) < PIT_PARK_MS) return Event::None;
        st = State::Parked;
        off.reset();
        return Event::Parked;
      case State::Parked:
        if (off.onFix(speedKmh, gpsTimeMs)) {
          st = State::Driving;
          slow = false;
          return Event::MovedOff;
        }
        if (since(stoppedMs, gpsTimeMs) < SESSION_RESUME_MS) return Event::None;
        st = State::Over;
        return Event::SessionOver;
      case State::Over:
        break;
    }
    return Event::None;
  }

  bool parked() const { return st != State::Driving; }

private:
  static constexpr uint32_t DAY_MS = 86400000UL;
  enum class State { Driving, Parked, Over };
  State st = State::Driving;
  bool slow = false;        // slower than PIT_PARK_KMH since...
  uint32_t stoppedMs = 0;   // ...this GPS time
  DriveOffDetector off;

  // GPS time from a to b, across midnight
  static uint32_t since(uint32_t a, uint32_t b) { return (b + DAY_MS - a) % DAY_MS; }
};

// The last session started, kept in flash so a power cut doesn't reset the count.
struct SessionCount {
  std::string trackId;
  std::string date;  // local "YYYYMMDD"
  int number = 0;
};

// The number of a session starting now at trackId: one more than the last
// if that was at the same track on the same local date, else 1. `stamp` is
// the session's local "YYYYMMDD_HHMMSS" (empty or another form when the
// GPS date isn't known: then it's 1). Updates `last`.
inline int nextSessionNumber(SessionCount& last, const std::string& trackId, const std::string& stamp) {
  bool dated = stamp.size() >= 9 && stamp[8] == '_';
  for (size_t i = 0; dated && i < 8; i++) dated = stamp[i] >= '0' && stamp[i] <= '9';
  const std::string date = dated ? stamp.substr(0, 8) : std::string();
  const bool sameDay = dated && last.number > 0 && last.trackId == trackId && last.date == date;
  last.trackId = trackId;
  last.date = date;
  last.number = sameDay ? last.number + 1 : 1;
  return last.number;
}

#endif  // AUTO_START_H
