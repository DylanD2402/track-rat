/**
 * Track Rat - Power implementation
 */

#include "Power.h"
#include "Battery.h"
#include "BatteryStats.h"
#include "Config.h"
#include "SettingsData.h"
#include "WifiPortal.h"
#include <Preferences.h>
#include "driver/gpio.h"
#include "esp_private/esp_clk.h"
#include "esp_sleep.h"

namespace Power {

static battery::Monitor monitor({BATTERY_PRESENT_MIN_MV, BATTERY_PRESENT_MAX_MV, BATTERY_LOW_MV, BATTERY_LOW_HOLD_MS,
                                 BATTERY_PRESENT_HYST_MV, BATTERY_ABSENT_SAMPLES, BATTERY_CHARGE_STEP_MV,
                                 BATTERY_CHARGE_OFFSET_MV, BATTERY_TREND_RISE_MV,
                                 BATTERY_TREND_FALL_MV, BATTERY_CLIMB_MIN, BATTERY_CLIMB_RISE_MV,
                                 BATTERY_CLIMB_DIP_MV, BATTERY_CLIMB_JUMP_MV, BATTERY_SHOWN_CHARGE_MS, BATTERY_SHOWN_DISCHARGE_MS});
static uint32_t lastRead = 0;
static uint32_t lastLog = 0;
static bool firstRead = true;
static bool lastCharging = false;
static bool lastUsbHost = false;
RTC_DATA_ATTR static bool switchedOff = false;  // the last sleep was a power off (kept through deep sleep only)

#if BATTERY_ENABLED
// What the battery has been through (BatteryStats.h). Changed only by the loop; the Wi-Fi page reads a copy.
static battery::Stats stats({BATTERY_CAPACITY_MAH,
                             {BATTERY_DRAW_SCREEN_MA, BATTERY_DRAW_WIFI_MA, BATTERY_DRAW_SCREEN_OFF_MA, BATTERY_DRAW_SLEEP_MA}});
static battery::MinuteMeter meter;
static portMUX_TYPE statsLock = portMUX_INITIALIZER_UNLOCKED;
static Preferences battPrefs;  // namespace "batt"
static bool battOpen = false;
static uint32_t lastStatsSave = 0;
// What the screens and the Wi-Fi page are told, copied once a second
static volatile int snapPercent = -1, snapMv = 0;
static volatile bool snapCharging = false;
static volatile uint8_t snapMode = 0;
// Where the board went to sleep: its voltage and the RTC clock (which runs through deep sleep)
static const uint32_t SLEEP_NOTE_MAGIC = 0x534C5001;
struct SleepNote {
  uint32_t magic;
  int32_t mv;
  uint64_t rtcUs;
};
RTC_DATA_ATTR static SleepNote sleepNote;
static int readBatteryMv(int* pinMvOut);
static bool backlightIsOn();
#endif

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

#if BATTERY_ENABLED
static void loadStats() {
  battOpen = battPrefs.begin("batt", false);
  if (!battOpen) {
    DEBUG_PRINTLN("Battery: flash storage unavailable - usage figures won't be kept");
    return;
  }
  battery::StatsData d;
  if (battPrefs.getBytesLength("stats") == sizeof(d) && battPrefs.getBytes("stats", &d, sizeof(d)) == sizeof(d)) {
    stats.load(d);  // (thrown away if it isn't good)
  }
  monitor.setFullMv(stats.fullMv());
}

// Written when it changed and at most every BATTERY_STATS_SAVE_MS, and before sleeping: flash wears
static void saveStats() {
  if (!battOpen || !stats.dirty()) return;
  const battery::StatsData d = stats.data();
  battPrefs.putBytes("stats", &d, sizeof(d));
  portENTER_CRITICAL(&statsLock);
  stats.saved();
  portEXIT_CRITICAL(&statsLock);
}

static battery::Mode currentMode() {
  if (!backlightIsOn()) return battery::Mode::ScreenOff;
  return WifiPortal::active() ? battery::Mode::ScreenOnWifi : battery::Mode::ScreenOn;
}
#endif

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
  loadStats();
  // Woken from deep sleep: what the battery did while the board slept (nothing is learned if it was charged)
  if (sleepNote.magic == SLEEP_NOTE_MAGIC) {
    sleepNote.magic = 0;
    const uint64_t nowUs = esp_clk_rtc_time();
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED && nowUs > sleepNote.rtcUs) {
      const uint32_t secs = (uint32_t)((nowUs - sleepNote.rtcUs) / 1000000ULL);
      const int wakeMv = readBatteryMv(nullptr);
      portENTER_CRITICAL(&statsLock);
      stats.addSleep(secs, sleepNote.mv, wakeMv);
      portEXIT_CRITICAL(&statsLock);
      DEBUG_PRINTF("Battery: slept %lu s, %d mV to %d mV\n", (unsigned long)secs, (int)sleepNote.mv, wakeMv);
    }
  }
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
// Kept in RTC memory, so it survives deep sleep and resets (not a power cut). A row marked "start" is the
// first reading after the board started again: the time before it is a gap.
static const uint32_t HISTORY_MAGIC = 0x42415448;
RTC_NOINIT_ATTR static uint32_t historyMagic;
RTC_NOINIT_ATTR static int16_t historyMv[HISTORY];
RTC_NOINIT_ATTR static uint8_t historyFlags[HISTORY];  // bit 0: USB host, 1: backlight on, 2: charging, 3: start
RTC_NOINIT_ATTR static int historyNext, historyCount;
static bool historyStarted = false;
static bool backlightIsOn();

static void printHistory() {
  DEBUG_PRINTF("Battery history: %d readings, 1 s apart, oldest first (n, mV, usb, backlight, charging, start)\n", historyCount);
  for (int i = 0; i < historyCount; i++) {
    int k = (historyNext - historyCount + i + HISTORY) % HISTORY;
    uint8_t f = historyFlags[k];
    DEBUG_PRINTF("%d,%d,%d,%d,%d,%d\n", i - historyCount + 1, historyMv[k], f & 1, (f >> 1) & 1, (f >> 2) & 1,
                 (f >> 3) & 1);
    delay(2);  // let the USB serial keep up
  }
  DEBUG_PRINTLN("Battery history: end");
}
#endif

// The one place that reads the USB serial: 'b' prints the battery history here, any other key waits for
// takeTestKey() (main.cpp acts on 'p' and 'r', the Bluetooth pause test)
static char testKey = 0;
char takeTestKey() {
  const char k = testKey;
  testKey = 0;
  return k;
}

static void readSerialKeys() {
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  while (Serial.available()) {
    const int c = Serial.read();
#if BATTERY_ENABLED
    if (c == 'b') printHistory();
#endif
    if (c == 'p' || c == 'r') testKey = (char)c;
  }
#endif
}

void update(uint32_t nowMs) {
  readSerialKeys();
#if BATTERY_ENABLED
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
  // What the battery has been through: the plug-in steps, and once a minute the mode and the mean voltage
  const battery::Mode mode = currentMode();
  snapMode = (uint8_t)mode;
  snapPercent = monitor.percent();
  snapMv = monitor.millivolts();
  snapCharging = monitor.charging();
  if (monitor.present()) {
    int stepMv = 0, fromMv = 0;
    if (monitor.takeStep(stepMv, fromMv)) {
      portENTER_CRITICAL(&statsLock);
      stats.addStep(stepMv, fromMv);
      portEXIT_CRITICAL(&statsLock);
      DEBUG_PRINTF("Battery: plug-in step %d mV from %d mV\n", stepMv, fromMv);
    }
    if (meter.add(mv, mode, monitor.charging())) {
      portENTER_CRITICAL(&statsLock);
      stats.addMinute(meter.mode(), meter.meanMv(), meter.charging(), monitor.percent());
      portEXIT_CRITICAL(&statsLock);
      if (stats.takeFull()) {  // the charger finished: this cell's full voltage
        monitor.setFullMv(stats.fullMv());
        monitor.markFull();
        DEBUG_PRINTF("Battery: full reads %d mV\n", stats.fullMv());
      }
    }
  } else {
    meter.reset();
  }
  if (nowMs - lastStatsSave >= BATTERY_STATS_SAVE_MS) {
    lastStatsSave = nowMs;
    saveStats();
  }

  if (historyMagic != HISTORY_MAGIC || historyNext < 0 || historyNext >= HISTORY || historyCount < 0 ||
      historyCount > HISTORY) {
    historyMagic = HISTORY_MAGIC;  // power cut: RTC memory holds garbage
    historyNext = historyCount = 0;
  }
  historyMv[historyNext] = (int16_t)mv;
  historyFlags[historyNext] = (usbHost ? 1 : 0) | (backlightIsOn() ? 2 : 0) | (monitor.charging() ? 4 : 0) |
                              (historyStarted ? 0 : 8);
  historyStarted = true;
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

battery::Report batteryReport() {
#if BATTERY_ENABLED
  portENTER_CRITICAL(&statsLock);
  const battery::Stats copy = stats;  // (the loop changes it; this may be the web server's task)
  portEXIT_CRITICAL(&statsLock);
  const int percent = snapPercent;
  const bool charging = snapCharging;
  battery::Report r = copy.report((battery::Mode)snapMode, percent, charging);
  r.present = percent >= 0;
  r.percent = percent;
  r.mv = snapMv;
  r.charging = charging;
  return r;
#else
  return battery::Report();
#endif
}

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

#if BATTERY_ENABLED
  // Down to the low cutoff: a run from full ends here. Then everything is saved, and where it went to sleep is
  // kept in RTC memory for the next start.
  if (monitor.present()) {
    if (monitor.low(millis())) {
      portENTER_CRITICAL(&statsLock);
      stats.lowReached();
      portEXIT_CRITICAL(&statsLock);
    }
    saveStats();
    sleepNote.mv = monitor.millivolts();
    sleepNote.rtcUs = esp_clk_rtc_time();
    sleepNote.magic = SLEEP_NOTE_MAGIC;
  }
#endif

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
