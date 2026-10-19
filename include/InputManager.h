/**
 * Track Rat - Input
 *
 * Touch (FT6336G capacitive panel) and the on-board BOOT button.
 *   Touch: tap the top bar, or the left / centre / right third below it, or hold.
 *   BOOT:  short press, or long press (fires while still held).
 *   Big button (optional, EXT_BUTTON_PIN): same as BOOT, reported as BootShort / BootLong.
 */

#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>

enum class InputEvent { None, TapLeft, TapCenter, TapRight, TapHeader, Hold, BootShort, BootLong };

class InputManager {
public:
  void begin();
  // Start the optional big button unless the GPS is using its pin. Call after gps.begin().
  void beginExtButton(int gpsRxPin, int gpsTxPin);
  InputEvent poll();

  bool touchAvailable() const { return touchOk; }
  // How long the screen has been held so far (0 if not touched). For UI feedback.
  uint32_t touchHeldMs() const;
  // Screen y of the last tap, for screens with more than one row of buttons.
  int16_t lastTapY() const { return tapY; }

private:
  bool touchOk = false;
  uint32_t lastPoll = 0;

  bool touching = false;
  bool holdFired = false;
  uint32_t touchStart = 0;
  int16_t startX = 0, startY = 0;
  int16_t tapY = 0;

  struct Button {
    bool down = false;
    bool longFired = false;
    uint32_t start = 0;
  };
  Button boot, ext;
  int extPin = -1;
  InputEvent pollButton(Button& b, bool pressed, uint32_t now);

  bool readTouch(int16_t& x, int16_t& y);
};

#endif  // INPUT_MANAGER_H
