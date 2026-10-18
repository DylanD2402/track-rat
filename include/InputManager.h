/**
 * Track Rat - Input
 *
 * Touch (FT6336G capacitive panel) and the on-board BOOT button.
 *   Touch: tap the left / centre / right third of the screen, or hold.
 *   BOOT:  short press, or long press (fires while still held).
 */

#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>

enum class InputEvent { None, TapLeft, TapCenter, TapRight, Hold, BootShort, BootLong };

class InputManager {
public:
  void begin();
  InputEvent poll();

  bool touchAvailable() const { return touchOk; }
  // How long the screen has been held so far (0 if not touched). For UI feedback.
  uint32_t touchHeldMs() const;

private:
  bool touchOk = false;
  uint32_t lastPoll = 0;

  bool touching = false;
  bool holdFired = false;
  uint32_t touchStart = 0;
  int16_t startX = 0, startY = 0;

  bool bootDown = false;
  bool bootLongFired = false;
  uint32_t bootStart = 0;

  bool readTouch(int16_t& x, int16_t& y);
};

#endif  // INPUT_MANAGER_H
