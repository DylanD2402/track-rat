/**
 * Track Rat - Input
 *
 * Touch (FT6336G capacitive panel) and the on-board BOOT button.
 *   Touch: tap the top bar, or the left / centre / right third below it, or hold.
 *   BOOT:  short press, or long press (fires while still held).
 */

#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>

enum class InputEvent { None, TapLeft, TapCenter, TapRight, TapHeader, Hold, BootShort, BootLong };

class InputManager {
public:
  void begin();
  InputEvent poll();

  bool touchAvailable() const { return touchOk; }
  // How long the screen has been held so far (0 if not touched). For UI feedback.
  uint32_t touchHeldMs() const;
  // Where the touch that produced the last Tap event began (screen pixels).
  int16_t lastTapY() const { return tapY; }

private:
  bool touchOk = false;
  uint32_t lastPoll = 0;

  bool touching = false;
  bool holdFired = false;
  uint32_t touchStart = 0;
  int16_t startX = 0, startY = 0;
  int16_t tapY = 0;

  bool bootDown = false;
  bool bootLongFired = false;
  uint32_t bootStart = 0;

  bool readTouch(int16_t& x, int16_t& y);
};

#endif  // INPUT_MANAGER_H
