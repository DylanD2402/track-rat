/**
 * Track Rat - Auto-sleep decision (no Arduino code, so it is tested on the PC)
 *
 * Call activity() whenever the user does something or the car is moving, and
 * update() every loop. After idleMs of neither, the screen goes off. If
 * still nothing happens for another deepAfterMs, update() says to deep sleep.
 * Any activity before that turns the screen back on.
 */

#ifndef SLEEP_TIMER_H
#define SLEEP_TIMER_H

#include <stdint.h>

enum class SleepAction { None, ScreenOff, DeepSleep };

class SleepTimer {
public:
  SleepTimer(uint32_t idleMs, uint32_t deepAfterMs) : idle(idleMs), deepAfter(deepAfterMs) {}

  void begin(uint32_t nowMs) {
    lastActivity = nowMs;
    off = false;
  }

  // Returns true if this woke the screen (so the caller can switch the backlight on).
  bool activity(uint32_t nowMs) {
    lastActivity = nowMs;
    bool wasOff = off;
    off = false;
    return wasOff;
  }

  bool screenOff() const { return off; }

  // movingNow: the car is moving at this moment. That counts as activity, so a session is never put to sleep.
  SleepAction update(uint32_t nowMs, bool movingNow) {
    if (movingNow) activity(nowMs);
    uint32_t idleFor = nowMs - lastActivity;  // unsigned: survives millis() wrapping
    if (off) return idleFor >= idle + deepAfter ? SleepAction::DeepSleep : SleepAction::None;
    if (idleFor >= idle) {
      off = true;
      return SleepAction::ScreenOff;
    }
    return SleepAction::None;
  }

private:
  uint32_t idle, deepAfter;
  uint32_t lastActivity = 0;
  bool off = false;
};

#endif  // SLEEP_TIMER_H
