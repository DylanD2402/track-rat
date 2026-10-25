/**
 * Track Rat - Input
 *
 * Touch (FT6336G capacitive panel) and the on-board BOOT button.
 *   Touch: tap the top bar, or the left / centre / right third below it, or hold. A finger that moves
 *          is a drag (dragging(), dragDy()) and gives no tap.
 *   BOOT:  short press, or long press, given when it's let go between LONG_PRESS_MS and
 *          POWER_OFF_BAR_MS. Held on to POWER_OFF_HOLD_MS, it switches the unit off: main.cpp watches
 *          bootHeldMs().
 *   Big button (optional, EXT_BUTTON_PIN): BootShort / BootLong too, but its long press fires at
 *          LONG_PRESS_MS while still held, and it doesn't switch off.
 * A button still held from before begin() (the press that woke the board) counts once it's let go.
 */

#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>
#include "ButtonPress.h"
#include "Config.h"

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
  // How long BOOT has been held so far (0 if it isn't, or if it was already down at start-up)
  uint32_t bootHeldMs() const { return boot.heldMs(millis()); }
  // Screen x and y of the last tap, for screens with more than one row of buttons.
  int16_t lastTapX() const { return tapX; }
  int16_t lastTapY() const { return tapY; }
  // How long the finger was down for the last tap (the timing screen ignores quick ones while moving).
  // A finger that moves more than DRAG_PX from where it touched down is dragging, not tapping:
  // no Tap or Hold comes of it. dragDy() is how far it has moved up or down since it touched down.
  bool dragging() const { return touching && dragged; }
  int16_t dragDy() const { return touching ? curY - startY : 0; }
  // The screen is upside down (rotation other than DISPLAY_ROTATION): turn touches round with it.
  void setUpsideDown(bool on) { flipped = on; }

private:
  bool touchOk = false;
  bool flipped = false;
  uint32_t lastPoll = 0;

  bool touching = false;
  bool holdFired = false;
  uint32_t touchStart = 0;
  bool dragged = false;
  int16_t startX = 0, startY = 0;
  int16_t curX = 0, curY = 0;
  int16_t tapX = 0, tapY = 0;

  ButtonPress boot{LONG_PRESS_MS, POWER_OFF_BAR_MS}, ext{LONG_PRESS_MS};
  int extPin = -1;
  static InputEvent bootEvent(ButtonPress::Event e);

  bool readTouch(int16_t& x, int16_t& y);
};

#endif  // INPUT_MANAGER_H
