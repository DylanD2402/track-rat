/**
 * Track Rat - Input Implementation
 */

#include "InputManager.h"
#include "Config.h"
#include <Wire.h>

static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
static const uint32_t TAP_MAX_MS = 800;

void InputManager::begin() {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

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

  static bool loggedThisTouch = false;
  if (!touching) loggedThisTouch = false;
  if (!loggedThisTouch) {
    DEBUG_PRINTF("Touch: raw=(%d,%d) screen=(%d,%d)\n", rawX, rawY, x, y);
    loggedThisTouch = true;
  }
  return true;
}

InputEvent InputManager::poll() {
  uint32_t now = millis();
  if (now - lastPoll < 20) return InputEvent::None;
  lastPoll = now;

  // BOOT button
  bool down = digitalRead(BOOT_BUTTON_PIN) == LOW;
  if (down && !bootDown) {
    bootDown = true;
    bootLongFired = false;
    bootStart = now;
  } else if (down && bootDown && !bootLongFired && now - bootStart >= LONG_PRESS_MS) {
    bootLongFired = true;
    return InputEvent::BootLong;
  } else if (!down && bootDown) {
    bootDown = false;
    if (!bootLongFired && now - bootStart >= 30) return InputEvent::BootShort;
  }

  if (!touchOk) return InputEvent::None;

  int16_t x, y;
  bool pressed = readTouch(x, y);
  if (pressed && !touching) {
    touching = true;
    holdFired = false;
    touchStart = now;
    startX = x;
    startY = y;
  } else if (pressed && touching && !holdFired && now - touchStart >= END_SESSION_HOLD_MS) {
    holdFired = true;
    return InputEvent::Hold;
  } else if (!pressed && touching) {
    touching = false;
    if (!holdFired && now - touchStart <= TAP_MAX_MS) {
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
  if (!touching || holdFired) return 0;
  return millis() - touchStart;
}
