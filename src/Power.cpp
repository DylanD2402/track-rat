/**
 * Track Rat - Power implementation
 */

#include "Power.h"
#include "Battery.h"
#include "Config.h"
#include "SettingsData.h"
#include "driver/gpio.h"
#include "esp_sleep.h"

namespace Power {

static battery::Monitor monitor({BATTERY_PRESENT_MIN_MV, BATTERY_PRESENT_MAX_MV, BATTERY_LOW_MV, BATTERY_LOW_HOLD_MS,
                                 BATTERY_PRESENT_HYST_MV, BATTERY_ABSENT_SAMPLES, BATTERY_CHARGE_STEP_MV,
                                 BATTERY_CHARGE_OFFSET_MV, BATTERY_CHARGE_HOLD_MV, BATTERY_TREND_RISE_MV,
                                 BATTERY_TREND_FALL_MV, BATTERY_CLIMB_MIN, BATTERY_CLIMB_RISE_MV,
                                 BATTERY_CLIMB_DIP_MV, BATTERY_CLIMB_JUMP_MV, BATTERY_SHOWN_CHARGE_MS, BATTERY_SHOWN_DISCHARGE_MS});
static uint32_t lastRead = 0;
static uint32_t lastLog = 0;
static bool firstRead = true;
static bool lastCharging = false;
static bool lastUsbHost = false;
RTC_DATA_ATTR static bool switchedOff = false;  // the last sleep was a power off (kept through deep sleep only)

static const char* wakeCauseName(esp_sleep_wakeup_cause_t c) {
  switch (c) {
    case ESP_SLEEP_WAKEUP_EXT0: return "BOOT button";
    case ESP_SLEEP_WAKEUP_EXT1: return esp_sleep_get_ext1_wakeup_status() & (1ULL << BOOT_BUTTON_PIN) ? "BOOT button" : "touch";
    case ESP_SLEEP_WAKEUP_TIMER: return "the timer (a button or touch was held down)";
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "power on / reset";
    default: return "other";
  }
}

[[noreturn]] static void sleepNow(bool switchingOff);

void begin() {
  // Switched off with BOOT or a finger still down (see deepSleep): it woke to look again. Still off, so back
  // to sleep with the backlight and the GPS's line still held, before anything else starts.
  if (switchedOff && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) sleepNow(true);
  switchedOff = false;
#ifdef TFT_BL
  gpio_hold_dis((gpio_num_t)TFT_BL);  // held low through deep sleep
  gpio_deep_sleep_hold_dis();
#endif
  DEBUG_PRINTF("Power: started by %s\n", wakeCauseName(esp_sleep_get_wakeup_cause()));
#if BATTERY_ENABLED
  pinMode(BATTERY_ADC_PIN, INPUT);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);  // full range to ~3.1 V; the pin sees at most ~2.2 V
#endif
}

#if BATTERY_ENABLED
// Average a burst of readings. analogReadMilliVolts applies the chip's factory ADC calibration.
static int readBatteryMv(int* pinMvOut) {
  uint32_t sum = 0;
  for (int i = 0; i < BATTERY_SAMPLES; i++) {
    sum += analogReadMilliVolts(BATTERY_ADC_PIN);
  }
  int pinMv = (int)(sum / BATTERY_SAMPLES);
  if (pinMvOut) *pinMvOut = pinMv;
  return battery::batteryMvFromPinMv(pinMv, BATTERY_DIVIDER_RATIO, BATTERY_CAL_OFFSET_MV);
}
#endif

#if BATTERY_ENABLED
// The last 10 minutes of readings, 1 s apart, for measuring what plugging in and unplugging do on a real
// board: send "b" over the USB serial port and they are printed (oldest first).
static const int HISTORY = 600;
static int16_t historyMv[HISTORY];
static uint8_t historyFlags[HISTORY];  // bit 0: USB host, bit 1: backlight on, bit 2: charging
static int historyNext = 0, historyCount = 0;
static bool backlightIsOn();

static void printHistory() {
  DEBUG_PRINTF("Battery history: %d readings, 1 s apart, oldest first (s, mV, usb, backlight, charging)\n", historyCount);
  for (int i = 0; i < historyCount; i++) {
    int k = (historyNext - historyCount + i + HISTORY) % HISTORY;
    uint8_t f = historyFlags[k];
    DEBUG_PRINTF("%d,%d,%d,%d,%d\n", i - historyCount + 1, historyMv[k], f & 1, (f >> 1) & 1, (f >> 2) & 1);
    delay(2);  // let the USB serial keep up
  }
  DEBUG_PRINTLN("Battery history: end");
}
#endif

void update(uint32_t nowMs) {
#if BATTERY_ENABLED
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  while (Serial.available()) {
    if (Serial.read() == 'b') printHistory();
  }
#endif
  if (!firstRead && nowMs - lastRead < BATTERY_READ_MS) return;
  firstRead = false;
  lastRead = nowMs;

  int pinMv = 0;
  int mv = readBatteryMv(&pinMv);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  const bool usbHost = HWCDC::isPlugged();  // a PC or car port is sending frames (a wall charger isn't)
#else
  const bool usbHost = false;
#endif
  if (usbHost != lastUsbHost) {
    lastUsbHost = usbHost;
    DEBUG_PRINTF("Battery: USB host %s\n", usbHost ? "seen" : "gone");
  }
  monitor.setUsbHost(usbHost);
  monitor.addSample(nowMs, mv);
  if (monitor.charging() != lastCharging) {
    lastCharging = monitor.charging();
    DEBUG_PRINTF("Battery: %s (average %d mV)\n", lastCharging ? "charging" : "not charging", monitor.millivolts());
  }
  historyMv[historyNext] = (int16_t)mv;
  historyFlags[historyNext] = (usbHost ? 1 : 0) | (backlightIsOn() ? 2 : 0) | (monitor.charging() ? 4 : 0);
  historyNext = (historyNext + 1) % HISTORY;
  if (historyCount < HISTORY) historyCount++;

  if (lastLog == 0 || nowMs - lastLog >= 10000) {
    lastLog = nowMs ? nowMs : 1;
    if (monitor.present()) {
      DEBUG_PRINTF("Battery: pin %d mV, battery %d mV (average %d mV), %d%%, %s\n", pinMv, mv, monitor.millivolts(),
                   monitor.percent(), monitor.charging() ? "charging" : "not charging");
    } else {
      DEBUG_PRINTF("Battery: none fitted (pin %d mV, reads %d mV)\n", pinMv, mv);
    }
  }
#else
  (void)nowMs;
#endif
}

int batteryPercent() { return BATTERY_ENABLED ? monitor.percent() : -1; }
bool batteryCharging() { return BATTERY_ENABLED && monitor.charging(); }
int batteryMillivolts() { return BATTERY_ENABLED ? monitor.millivolts() : 0; }
bool batteryLow(uint32_t nowMs) { return BATTERY_ENABLED && monitor.low(nowMs); }

// The backlight pin is PWM (LEDC) while it's on, so it can be dimmed, and a
// plain low output while it's off, which deep sleep can hold.
static uint8_t brightnessPct = 100;
static bool backlightOn = false;
#if BATTERY_ENABLED
static bool backlightIsOn() { return backlightOn; }
#endif

static void writeBacklight() {
#ifdef TFT_BL
  static bool ledcReady = false;
  if (!ledcReady) {
    ledcSetup(BACKLIGHT_PWM_CHANNEL, BACKLIGHT_PWM_HZ, BACKLIGHT_PWM_BITS);
    ledcReady = true;
  }
  if (backlightOn) {
    ledcAttachPin(TFT_BL, BACKLIGHT_PWM_CHANNEL);
    ledcWrite(BACKLIGHT_PWM_CHANNEL, settings::backlightDuty(brightnessPct, (1u << BACKLIGHT_PWM_BITS) - 1));
  } else {
    ledcDetachPin(TFT_BL);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);
  }
#endif
}

void backlight(bool on) {
  backlightOn = on;
  writeBacklight();
}

void setBrightness(uint8_t percent) {
  if (percent == brightnessPct) return;
  brightnessPct = percent;
  if (backlightOn) writeBacklight();  // off stays off (screen asleep): the new level shows when it wakes
}

void deepSleep(bool switchingOff) {
  DEBUG_PRINTLN(switchingOff ? "Power: off (a tap or BOOT turns it on)" : "Power: deep sleep (BOOT or touch wakes it)");
  delay(50);  // let the log line out (Serial.flush() could wait on a USB host that isn't there)

  backlight(false);
  // A finger or button still down would wake it again at once. Held to switch off, BOOT is still down
  // now: with the screen dark, it's let go.
  uint32_t start = millis();
  const uint32_t waitMs = switchingOff ? 10000 : 3000;
  while ((digitalRead(BOOT_BUTTON_PIN) == LOW || digitalRead(TOUCH_INT) == LOW) && millis() - start < waitMs) delay(10);
  sleepNow(switchingOff);
}

static void sleepNow(bool switchingOff) {
#ifdef TFT_BL
  gpio_hold_en((gpio_num_t)TFT_BL);  // keep the backlight off while the chip sleeps
  gpio_deep_sleep_hold_en();
#endif
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);  // (from begin(), nothing has set them up yet)
  pinMode(TOUCH_INT, INPUT);
  // BOOT is IO0 (pulled up on the board, low when pressed); touch INT is IO17 (low while touched).
  // A pin that is still low now (stuck button, touch chip holding INT low) would wake the board
  // immediately, so it is left out of this sleep.
  uint64_t mask = 0;
  if (digitalRead(BOOT_BUTTON_PIN) == HIGH) mask |= 1ULL << BOOT_BUTTON_PIN;
  if (digitalRead(TOUCH_INT) == HIGH) mask |= 1ULL << TOUCH_INT;
  if (mask) {
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  } else {
    esp_sleep_enable_timer_wakeup(10ULL * 1000000ULL);  // nothing usable to wake on: try again in 10 s
  }
  switchedOff = switchingOff;
  esp_deep_sleep_start();
  while (true) {}  // never reached
}

}  // namespace Power
