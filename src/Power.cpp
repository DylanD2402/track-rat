/**
 * Track Rat - Power implementation
 */

#include "Power.h"
#include "Battery.h"
#include "Config.h"
#include "driver/gpio.h"
#include "esp_sleep.h"

namespace Power {

static battery::Monitor monitor({BATTERY_PRESENT_MIN_MV, BATTERY_PRESENT_MAX_MV, BATTERY_LOW_MV, BATTERY_LOW_HOLD_MS,
                                 BATTERY_PRESENT_HYST_MV, BATTERY_ABSENT_SAMPLES});
static uint32_t lastRead = 0;
static uint32_t lastLog = 0;
static bool firstRead = true;

static const char* wakeCauseName(esp_sleep_wakeup_cause_t c) {
  switch (c) {
    case ESP_SLEEP_WAKEUP_EXT0: return "BOOT button";
    case ESP_SLEEP_WAKEUP_EXT1: return "BOOT button or touch";
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "power on / reset";
    default: return "other";
  }
}

void begin() {
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

void update(uint32_t nowMs) {
#if BATTERY_ENABLED
  if (!firstRead && nowMs - lastRead < BATTERY_READ_MS) return;
  firstRead = false;
  lastRead = nowMs;

  int pinMv = 0;
  int mv = readBatteryMv(&pinMv);
  monitor.addSample(nowMs, mv);

  if (lastLog == 0 || nowMs - lastLog >= 10000) {
    lastLog = nowMs ? nowMs : 1;
    if (monitor.present()) {
      DEBUG_PRINTF("Battery: pin %d mV, battery %d mV (average %d mV), %d%%\n", pinMv, mv, monitor.millivolts(),
                   monitor.percent());
    } else {
      DEBUG_PRINTF("Battery: none fitted (pin %d mV, reads %d mV)\n", pinMv, mv);
    }
  }
#else
  (void)nowMs;
#endif
}

bool batteryPresent() { return BATTERY_ENABLED && monitor.present(); }
int batteryPercent() { return BATTERY_ENABLED ? monitor.percent() : -1; }
int batteryMillivolts() { return BATTERY_ENABLED ? monitor.millivolts() : 0; }
bool batteryLow(uint32_t nowMs) { return BATTERY_ENABLED && monitor.low(nowMs); }

void backlight(bool on) {
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, on ? HIGH : LOW);
#else
  (void)on;
#endif
}

void deepSleep() {
  DEBUG_PRINTLN("Power: deep sleep (BOOT or touch wakes it)");
  delay(50);  // let the log line out (Serial.flush() could wait on a USB host that isn't there)

  backlight(false);
#ifdef TFT_BL
  gpio_hold_en((gpio_num_t)TFT_BL);  // keep the backlight off while the chip sleeps
  gpio_deep_sleep_hold_en();
#endif

  // A finger or button still down would wake it again at once.
  uint32_t start = millis();
  while ((digitalRead(BOOT_BUTTON_PIN) == LOW || digitalRead(TOUCH_INT) == LOW) && millis() - start < 3000) delay(10);

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
  esp_deep_sleep_start();
  while (true) {}  // never reached
}

}  // namespace Power
