/**
 * Track Rat - Input Implementation
 */

#include "InputManager.h"
#include "AddOns.h"
#include "Config.h"
#include <Wire.h>

static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
static const uint32_t TAP_MAX_MS = 800;
static const int DRAG_PX = 12;  // a finger that moves further than this is dragging, not tapping

void InputManager::begin() {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  boot.begin(digitalRead(BOOT_BUTTON_PIN) == LOW, millis());  // still held from waking the board: ignored until let go

  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(300);
  pinMode(TOUCH_INT, INPUT);

  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  touchOk = (Wire.endTransmission() == 0);
  DEBUG_PRINTF("Input: touch controller %s\n", touchOk ? "found" : "NOT found (use BOOT button)");
}

// The big button shares IO21 with one of the GPS pin pairs, so it can only
// be used once the GPS has been found somewhere else.
void InputManager::beginExtButton(int gpsRxPin, int gpsTxPin) {
#if EXT_BUTTON_ENABLED
  if (addOnPinTaken(EXT_BUTTON_PIN, gpsRxPin, gpsTxPin)) {
    DEBUG_PRINTF("Input: big button disabled - the GPS is on IO%d\n", EXT_BUTTON_PIN);
    return;
  }
  pinMode(EXT_BUTTON_PIN, INPUT_PULLUP);  // also undoes any GPS probe of this pin at boot
  extPin = EXT_BUTTON_PIN;
  ext.begin(digitalRead(extPin) == LOW, millis());
  DEBUG_PRINTF("Input: big button on IO%d (to GND)\n", EXT_BUTTON_PIN);
#else
  (void)gpsRxPin;
  (void)gpsTxPin;
#endif
}

bool InputManager::readTouch(int16_t& x, int16_t& y) {
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(0x02);  // TD_STATUS, followed by P1_XH, P1_XL, P1_YH, P1_YL
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)5) != 5) return false;

  uint8_t points = Wire.read() & 0x0F;
  uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  if (points == 0 || points > 2) return false;

  int16_t rawX = ((xh & 0x0F) << 8) | xl;  // portrait: 0..239
  int16_t rawY = ((yh & 0x0F) << 8) | yl;  // portrait: 0..319

#if TOUCH_SWAP_XY
  x = rawY;
  y = rawX;
#else
  x = rawX;
  y = rawY;
#endif
#if TOUCH_INVERT_X
  x = SCREEN_W - 1 - x;
#endif
#if TOUCH_INVERT_Y
  y = SCREEN_H - 1 - y;
#endif
  // The mapping above is for DISPLAY_ROTATION; with the screen turned the
  // other way up (the Screen setting), the touch panel is too.
  if (flipped) {
    x = SCREEN_W - 1 - x;
    y = SCREEN_H - 1 - y;
  }

  static bool loggedThisTouch = false;
  if (!touching) loggedThisTouch = false;
  if (!loggedThisTouch) {
    DEBUG_PRINTF("Touch: raw=(%d,%d) screen=(%d,%d)\n", rawX, rawY, x, y);
    loggedThisTouch = true;
  }
  return true;
}

// A button's press (ButtonPress.h): BootLong fires while still held, BootShort on release.
InputEvent InputManager::bootEvent(ButtonPress::Event e) {
  return e == ButtonPress::Event::Long ? InputEvent::BootLong : e == ButtonPress::Event::Short ? InputEvent::BootShort : InputEvent::None;
}

// FT6336G power mode register (ID_G_PMODE): 3 = hibernate, until its reset pin is pulsed
void InputManager::sleepTouch() {
  if (!touchOk) return;
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(0xA5);
  Wire.write(0x03);
  Wire.endTransmission();
}

InputEvent InputManager::poll() {
  uint32_t now = millis();
  if (now - lastPoll < 20) return InputEvent::None;
  lastPoll = now;

  // BOOT button, then the big button: both give the same events
  InputEvent be = bootEvent(boot.update(digitalRead(BOOT_BUTTON_PIN) == LOW, now));
  if (be != InputEvent::None) return be;
  if (extPin >= 0) {
    be = bootEvent(ext.update(digitalRead(extPin) == LOW, now));
    if (be != InputEvent::None) return be;
  }

  if (!touchOk) return InputEvent::None;

  int16_t x, y;
  bool pressed = readTouch(x, y);
  if (pressed && !touching) {
    touching = true;
    holdFired = false;
    dragged = false;
    touchStart = now;
    startX = curX = x;
    startY = curY = y;
  } else if (pressed && touching) {
    curX = x;
    curY = y;
    if (abs(x - startX) > DRAG_PX || abs(y - startY) > DRAG_PX) dragged = true;
    if (!holdFired && !dragged && now - touchStart >= END_SESSION_HOLD_MS) {
      holdFired = true;
      return InputEvent::Hold;
    }
  } else if (!pressed && touching) {
    touching = false;
    if (!holdFired && !dragged && now - touchStart <= TAP_MAX_MS) {
      tapX = startX;
      tapY = startY;
      if (startY < 30) return InputEvent::TapHeader;  // the top bar
      if (startX < SCREEN_W / 3) return InputEvent::TapLeft;
      if (startX >= SCREEN_W * 2 / 3) return InputEvent::TapRight;
      return InputEvent::TapCenter;
    }
  }
  return InputEvent::None;
}

uint32_t InputManager::touchHeldMs() const {
  if (!touching || holdFired || dragged) return 0;
  return millis() - touchStart;
}
