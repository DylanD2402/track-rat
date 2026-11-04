#include "SleepControl.h"

#include "App.h"
#include "Session.h"
#include "AppRollback.h"
#include "CameraBle.h"
#include "Config.h"
#include "LogWriter.h"
#include "Power.h"
#include "SessionLog.h"
#include "Settings.h"
#include "SleepTimer.h"
#include "UiTheme.h"
#include "WifiPortal.h"

namespace SleepControl {

static SleepTimer sleepTimer(AUTO_SLEEP_IDLE_MS, AUTO_SLEEP_DEEP_AFTER_MS);

// ---------------------------------------------------------------------------
// Sleep, power off and battery
// ---------------------------------------------------------------------------

static const uint32_t SLEEP_LOG_WAIT_MS = 3000;  // an SD write can take a few hundred ms

// Close and save whatever is open, so power can go. Anything else that must be
// flushed before sleep (e.g. an open log file) belongs here too.
static void saveForSleep() {
  Session::flushSummary();
  if (state == AppState::Running) enterState(AppState::Summary);  // saves the laps so far
  else if (state == AppState::FreeRoam) enterState(AppState::FreeRoamSummary);
  SessionLog::endLog();
  LogWriter::poll();
  // The writer task only closes the file when it next runs, and deep sleep would stop it first.
  if (!LogWriter::waitIdle(SLEEP_LOG_WAIT_MS)) DEBUG_PRINTLN("Log: card write still pending at sleep");
}

// Something happened (a touch, a press, the car moving). Returns true if the screen was off.
bool noteActivity(uint32_t now) {
  if (!sleepTimer.activity(now)) return false;
  display.setBacklight(true);
  lastRender = 0;
  return true;
}

// The GPS into standby (it keeps its satellites, so it has a fix again seconds after waking), the
// screen off, then deep sleep until the screen is touched or BOOT is pressed.

[[noreturn]] static void sleepNow(bool switchingOff) {
  approllback::markGood();  // switching off or sleeping is not a crash
  gps.standby();
  display.powerDown();
  Power::deepSleep(switchingOff);
}

static void lowBatterySleep() {
  DEBUG_PRINTF("Battery low (%d mV): saving and sleeping\n", Power::batteryMillivolts());
  saveForSleep();
  display.setBacklight(true);
  display.drawMessage("BATTERY LOW", "Saved. Going to sleep.", "Charge it, then touch or press BOOT", ui::C_YELLOW);
  delay(BATTERY_LOW_SCREEN_MS);
  sleepNow(false);
}

// Switching off: a recording that follows the sessions stops with the session that just ended (the next
// fix would have done it). Up to 2 s for the camera to say it has.
static void stopCameraRecording() {
  if (!Settings::get().cameraWithSessions || !CameraBle::connected()) return;
  CameraBle::setRecording(false);
  const uint32_t start = millis();
  while (millis() - start < 2000) {
    CameraBle::poll();
    const CameraBle::Status cs = CameraBle::status();
    if (!cs.cam.known || !cs.cam.recording()) return;
    delay(20);
  }
}

// Settings > Power off, or BOOT held for POWER_OFF_HOLD_MS: everything saved, then off until the screen is
// tapped (or BOOT pressed). It's auto-sleep's deep sleep straight away: nothing on the board can cut the battery.
[[noreturn]] void powerOff() {
  DEBUG_PRINTLN("Power off: saving, then off until the screen is tapped");
  display.setBacklight(true);
  display.drawMessage("POWER OFF", "Switching off…", "Tap the screen to turn it back on", ui::C_ORANGE);
  const uint32_t shown = millis();
  saveForSleep();
  stopCameraRecording();
  while (millis() - shown < POWER_OFF_MESSAGE_MS) delay(10);  // long enough to read
  sleepNow(true);
}

void begin(uint32_t now) { sleepTimer.begin(now); }

bool screenOff() { return sleepTimer.screenOff(); }

void poll(uint32_t now) {
  Power::update(now);
  display.setBatteryPercent(Power::batteryPercent());
  display.setBatteryCharging(Power::batteryCharging());
  if (Power::batteryLow(now)) lowBatterySleep();
#if AUTO_SLEEP_ENABLED
  // A phone on the Wi-Fi page counts as using the board, even with no taps:
  // don't turn the screen (and its upload code) off or sleep mid-download.
  static uint32_t lastWifiCheck = 0;
  static bool phoneOnWifi = false;
  if (now - lastWifiCheck >= 1000) {  // asking the Wi-Fi driver every loop is wasteful
    lastWifiCheck = now;
    phoneOnWifi = WifiPortal::active() && (WifiPortal::clientCount() > 0 || WifiPortal::busy());
  }
  if (phoneOnWifi && sleepTimer.screenOff()) noteActivity(now);
  switch (sleepTimer.update(now, phoneOnWifi)) {
    case SleepAction::ScreenOff:
      DEBUG_PRINTLN("Parked and idle: backlight off");
      display.setBacklight(false);
      break;
    case SleepAction::DeepSleep:
      saveForSleep();
      sleepNow(false);
      break;
    default:
      break;
  }
#endif
}

}  // namespace SleepControl
