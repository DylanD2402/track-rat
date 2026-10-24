/**
 * Track Rat - one push button's presses (no Arduino code, so it is tested on the PC)
 *
 * Call update() every poll with whether the button is down. A press let go
 * before longMs gives Short; one held to longMs gives Long while still held,
 * and nothing when it's let go. heldMs() is how long the current press has
 * lasted: BOOT held for POWER_OFF_HOLD_MS switches the unit off.
 *
 * A button already down at begin() gives nothing until it has been let go:
 * that is the press that woke the board, and a long one mustn't choose
 * something, or switch it off again.
 */

#ifndef BUTTON_PRESS_H
#define BUTTON_PRESS_H

#include <stdint.h>

class ButtonPress {
public:
  enum class Event { None, Short, Long };

  explicit ButtonPress(uint32_t longMs = 1000, uint32_t minMs = 30) : longAt(longMs), minShort(minMs) {}

  void begin(bool down, uint32_t nowMs) {
    isDown = ignored = down;
    longFired = false;
    start = nowMs;
  }

  Event update(bool down, uint32_t nowMs) {
    if (down && !isDown) {
      isDown = true;
      ignored = longFired = false;
      start = nowMs;
    } else if (down && !ignored && !longFired && nowMs - start >= longAt) {
      longFired = true;
      return Event::Long;
    } else if (!down && isDown) {
      isDown = false;
      const bool quiet = ignored || longFired;
      ignored = false;
      if (!quiet && nowMs - start >= minShort) return Event::Short;  // shorter: contact bounce
    }
    return Event::None;
  }

  // How long the current press has lasted (unsigned: survives millis() wrapping); 0 when up, or held since begin()
  uint32_t heldMs(uint32_t nowMs) const { return isDown && !ignored ? nowMs - start : 0; }

private:
  uint32_t longAt, minShort;
  bool isDown = false, ignored = false, longFired = false;
  uint32_t start = 0;
};

#endif  // BUTTON_PRESS_H
