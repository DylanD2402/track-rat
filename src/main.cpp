/**
 * Track Rat - GPS lap timer
 *
 * Board: LCDWiki ES3C28P (ESP32-S3 N16R8, 2.8" ILI9341V touch display, SD slot)
 * GPS:   Beitian BE-252Q (UART connector or expansion header, found at boot)
 *
 * Flow: logo -> GPS fix -> a track within 5 km? Already moving: a session starts
 *       on it (timing begins at the start/finish line). Parked: its Ready
 *       screen, which starts the session when the car drives off (the Menu is
 *       one button away). No track nearby: the home screen (Next up, Track
 *       day, Autocross, Drag, Drive). Tracks without a start/finish line get
 *       one by tapping the screen while crossing it.
 *
 * Sessions run themselves (AutoStart.h): parked for a minute shows the
 * summary; driving off within 10 minutes carries on, later it's the next
 * session of the day. Holding the screen still ends one.
 *
 * Away from known tracks, driving off from the menu starts Free Roam, which
 * watches for a loop driven twice (TrackLearn.h): then it's a new track,
 * timed from where the loop began.
 */

#include <Arduino.h>
#include <sys/time.h>
#include "app/App.h"
#include "app/AutocrossScreens.h"
#include "app/HomeScreen.h"
#include "app/PerformanceMode.h"
#include "app/SdUpdate.h"
#include "app/Session.h"
#include "app/SettingsScreens.h"
#include "app/SleepControl.h"
#include "app/TrackDay.h"
#include "AppRollback.h"
#include "AutoStart.h"
#include "ProfileStore.h"
#include "Config.h"
#include "CourseRuns.h"
#include "Courses.h"
#include "DisplayManager.h"
#include "FreeRoam.h"
#include "GPSHandler.h"
#include "GeoMath.h"
#include "CameraBle.h"
#include "CanObd.h"
#include "EngineData.h"
#include "ObdBle.h"
#include "Imu.h"
#include "InputManager.h"
#include "LocalTime.h"
#include "LapTimer.h"
#include "LogWriter.h"
#include "Power.h"
#include "RaceBoxBle.h"
#include "SessionLog.h"
#include "Settings.h"
#include "Storage.h"
#include "TrackDatabase.h"
#include "WifiPortal.h"

static const char* STATE_NAMES[] = {"GPS_WAIT", "MENU",      "TRACK_SELECT", "TRACK_CONFIRM",     "RUNNING",
                                    "SUMMARY",  "GPS_STATUS", "FREE_ROAM",   "FREE_ROAM_SUMMARY", "PERFORMANCE",
                                    "WIFI",     "SETUP",      "SETTINGS",     "ENGINE_DATA",       "CAMERA",
                                    "AUTOCROSS", "NEW_COURSE", "ADJUST_LINE", "BATTERY"};

DisplayManager display;
GPSHandler gps;
InputManager input;
LapTimer lapTimer;
TrackDatabase trackDB;
FreeRoam freeRoam;

AppState state = AppState::GpsWait;
static AppState wifiReturn = AppState::Menu;    // where the Wi-Fi screen goes back to
int selectedIndex = 0;

// Auto-start: a track was picked at boot with the car parked, so the Ready screen starts the session by itself once
// the car drives off (see AutoStart.h). It ends when the screen is left.
static bool autoStart = false;
static DriveOffDetector driveOff;

static bool nextSessionArmed = false;  // a session ended by itself: the summary starts the next one on driving off
// A paired DJI camera (CameraBle.h): it records while the session is moving, and
// when it started goes into the summary (6.4 lines the log up with the video)
static dji::FollowSessions cameraFollow;
GpsFix cameraFix;                         // the latest fix, to turn millis() into GPS time
bool haveCameraFix = false;
// The SYNC flash (6.4): a tap on the summary's or debrief's top bar shows SYNC and the GPS time
// for SYNC_FLASH_MS, for a camera to film; its GPS time goes into the summary
uint32_t syncUntil = 0;  // millis() it ends; 0: not showing
// The overheat warning (6.5): full screen for TEMP_ALERT_MS on any screen when coolant (or oil) reaches its limit
static tempalert::Alert tempAlert;
static void setClockFromGps(const GpsFix& f);

static uint32_t settingsApplied = 0;  // Settings::changeCount() when they were last applied

uint32_t lastRender = 0;
static uint32_t lastStatusLog = 0;

const Track& selectedTrack() { return *trackDB.get(selectedIndex); }

double distanceToTrack(int index) {
  const Track* t = trackDB.get(index);
  if (!gps.hasFix() || t->bench) return -1;
  return geo::distanceM(gps.lastFix().lat, gps.lastFix().lon, t->refLat(), t->refLon());
}

// Autocross courses (Courses.h), by trackDB index
std::vector<courses::Candidate> courseCandidates() {
  std::vector<courses::Candidate> out;
  for (int i = 0; i < trackDB.count(); i++) {
    const Track* t = trackDB.get(i);
    if (t->course && t->hasStartFinish) out.push_back({i, std::string(t->event.c_str()), t->startFinish.lat, t->startFinish.lon});
  }
  return out;
}

// Today's local date, "YYYYMMDD" ("" if the GPS hasn't given one)
std::string todayEvent() { return courses::dateOfStamp(std::string(gps.timestampString().c_str())); }

// trackDB index of today's course nearby (made today, start within 5 km), or -1
int todaysCourseNearby() {
  if (!gps.hasFix()) return -1;
  const std::vector<courses::Candidate> list = courseCandidates();
  const int k = courses::todaysNearby(list, todayEvent(), gps.lastFix().lat, gps.lastFix().lon);
  return k < 0 ? -1 : list[k].index;
}

void enterState(AppState next) {
  DEBUG_PRINTF("State: %s -> %s\n", STATE_NAMES[(int)state], STATE_NAMES[(int)next]);

  // A session's laps and files go with the driver and car it started with, even if they are changed meanwhile
  const bool sessionNext = next == AppState::Running || next == AppState::FreeRoam || next == AppState::Performance;
  if (sessionNext) Profiles::lockSession();

  Session::stateChanging(next);
  if (state == AppState::Summary) nextSessionArmed = false;  // set again by whoever ends a session by itself

  if (next == AppState::Performance) PerformanceMode::enter();
  if (state == AppState::Performance && next != AppState::Performance) PerformanceMode::leave();

  Session::freeRoamChanging(next);
  HomeScreen::stateChanging(next);

  AutocrossScreens::stateChanging(next);

  TrackDay::stateChanging(next);
  if (state == AppState::TrackConfirm && next != AppState::TrackConfirm) autoStart = false;

  // Wi-Fi is on only while its screen is shown, never while timing.
  if (next == AppState::Wifi) {
    flushSummary();  // the page lists and downloads the summaries
    wifiReturn = state;
    WifiPortal::start();
  } else if (state == AppState::Wifi) {
    WifiPortal::stop();
  }

  if (!sessionNext) Profiles::unlockSession();  // (the summaries and best laps above were saved under the session's pair)

  state = next;
  lastRender = 0;  // redraw now
}

// Is the Ready screen waiting for the car to drive off (AutoStart.h)
bool autoStartArmed() { return autoStart; }

// A track was picked at boot with the car parked: the Ready screen starts the session when it drives off
void armAutoStart() {
  autoStart = true;
  driveOff.reset();
}

// A session ended by itself: the summary starts the next one on driving off
void armNextSession() {
  nextSessionArmed = true;
  driveOff.reset();
}

// Make the current settings take effect: the screen and touch the right way
// up, the backlight, Bluetooth, and the time zone for file names. Called at
// boot and whenever they change, from the screen or the Wi-Fi page.
void applySettings() {
  const SettingsData s = Settings::get();
  settingsApplied = Settings::changeCount();
  display.setRotation(s.rotation);
  input.setUpsideDown(settings::upsideDown(s));
  Power::setBrightness(s.brightness);
#if BLE_ENABLED
  RaceBoxBle::setEnabled(s.bluetooth);
#endif
  gps.setUtcOffsetMinutes(s.utcOffsetMin);
  {  // the system clock's zone: the SD card's file dates are local time
    char tz[16];
    localtime_::posixTz(s.utcOffsetMin, tz, sizeof(tz));
    setenv("TZ", tz, 1);
    tzset();
  }
  lastRender = 0;
}

static void handleInput(InputEvent ev) {
  if (ev == InputEvent::None) return;
  if (tempAlert.showing(millis())) {  // any tap or press closes the overheat warning, and does nothing else
    tempAlert.dismiss();
    lastRender = 0;
    return;
  }
  // The camera's CAM/REC tag in the top bar: start or stop recording, on any screen that shows it
  if (ev == InputEvent::TapHeader && display.cameraTagHit(input.lastTapX())) {
    const CameraBle::Status cs = CameraBle::status();
    if (cs.cam.known) {
      CameraBle::toggleRecording();
      display.cameraAsked(cs.cam.recording());
      lastRender = 0;
    }
    return;
  }

  switch (state) {
    case AppState::GpsWait:
      HomeScreen::onInputGpsWait(ev);
      break;

    case AppState::Menu:  // the home screen: HomeLayout.h says where a tap lands; BOOT goes through them in order
      HomeScreen::onInputMenu(ev);
      break;

    case AppState::TrackSelect:
      TrackDay::onInputList(ev);
      break;

    case AppState::TrackConfirm:
      TrackDay::onInputReady(ev);
      break;

    case AppState::Running:
      Session::onInputRunning(ev);
      break;

    case AppState::FreeRoam:
      Session::onInputFreeRoam(ev);
      break;

    case AppState::Performance:
      PerformanceMode::onInput(ev);
      break;

    case AppState::Summary:  // MENU button, or on to this track's Ready screen to go out again
      Session::onInputSummary(ev);
      break;

    case AppState::FreeRoamSummary:
      Session::onInputFreeRoamSummary(ev);
      break;

    case AppState::Wifi:  // any tap or press turns Wi-Fi off, except mid firmware update
      if (ev != InputEvent::Hold && !WifiPortal::busy()) enterState(wifiReturn);
      break;

    case AppState::GpsStatus:
      HomeScreen::onInputGpsStatus(ev);
      break;

    case AppState::SettingsScreen:
      SettingsScreens::onInputList(ev);
      break;

    case AppState::EngineScreen:  // the list's taps are on what was drawn: engineFound
      SettingsScreens::onInputEngine(ev);
      break;

    case AppState::CameraScreen:  // the same screen, for the DJI camera
      SettingsScreens::onInputCamera(ev);
      break;

    case AppState::BatteryScreen:  // read-only: any tap or press goes back to the list
      SettingsScreens::onInputBattery();
      break;

    case AppState::Autocross:  // Start runs and New course on the top row; Adjust start, Adjust finish and Back below
      AutocrossScreens::onInputHub(ev);
      break;

    case AppState::AdjustLine:
      AutocrossScreens::onInputAdjust(ev);
      break;

    case AppState::NewCourse:  // Back on the left, Set start here across the other two thirds
      AutocrossScreens::onInputNewCourse(ev);
      break;

    case AppState::Setup:
      SettingsScreens::onInputSetup(ev);
      break;
  }
}

static void render() {
  uint32_t now = millis();
  const uint32_t bootHeld = input.bootHeldMs();
  if (bootHeld >= POWER_OFF_BAR_MS && !WifiPortal::busy()) {  // BOOT held: switching off unless it's let go
    display.drawPowerHold(bootHeld);
    return;
  }
  if (tempAlert.showing(now)) {  // the overheat warning covers everything, even the SYNC flash
    display.drawTempAlert(tempAlert.current().oil, tempAlert.current().c, tempAlert.redPhase(now));
    return;
  }
  if (syncUntil) {  // the SYNC flash covers everything, with the GPS time ticking
    if ((int32_t)(now - syncUntil) < 0 && haveCameraFix) {
      display.drawSync(gpsTimeAt(cameraFix, now).substring(11, 21).c_str());  // 14:03:12.3
      return;
    }
    syncUntil = 0;
  }
  Session::beforeRender(now);
  switch (state) {
    case AppState::GpsWait:
      HomeScreen::renderGpsWait();
      break;
    case AppState::GpsStatus:
      HomeScreen::renderGpsStatus();
      break;
    case AppState::Menu:
      HomeScreen::renderMenu(now);
      break;
    case AppState::TrackSelect:
      TrackDay::renderList();
      break;
    case AppState::TrackConfirm:
      TrackDay::renderReady();
      break;
    case AppState::Running:
      Session::renderRunning(now);
      break;
    case AppState::Summary:
      Session::renderSummary();
      break;
    case AppState::FreeRoam:
      Session::renderFreeRoam();
      break;
    case AppState::FreeRoamSummary:
      Session::renderFreeRoamSummary();
      break;
    case AppState::Performance:
      PerformanceMode::render();
      break;
    case AppState::SettingsScreen:
      SettingsScreens::renderList();
      break;
    case AppState::EngineScreen:
      SettingsScreens::renderEngine();
      break;
    case AppState::CameraScreen:
      SettingsScreens::renderCamera();
      break;
    case AppState::BatteryScreen:
      SettingsScreens::renderBattery();
      break;
    case AppState::Setup:
      SettingsScreens::renderSetup();
      break;
    case AppState::Autocross:
      AutocrossScreens::renderHub();
      break;
    case AppState::AdjustLine:
      AutocrossScreens::renderAdjust();
      break;
    case AppState::NewCourse:
      AutocrossScreens::renderNewCourse();
      break;
    case AppState::Wifi:
      display.drawWifi(WifiPortal::active(), WifiPortal::networkName(), WifiPortal::address(),
                       WifiPortal::clientCount(), WifiPortal::uploadCodeValue(), WifiPortal::updateProgress(),
                       WifiPortal::restarting(), input.touchAvailable());
      break;
  }
}

// Overrides the Arduino core's: it would call a new firmware good before setup() even starts, and then a bad one
// could never be rolled back. loop() says so after HEALTHY_AFTER_MS (see AppRollback.h).
extern "C" bool verifyRollbackLater() { return true; }

void setup() {
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  Serial.setTxBufferSize(1024);  // (before begin) a burst of log lines fits while the PC catches up
#endif
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  // A USB host that isn't reading gets 1 ms, then the log is dropped until it reads again. With no host at all
  // (car power) prints never wait. A PC with no serial monitor open, or a car's media USB port, has the board
  // plugged in but never reads, and with 0 here the core's retry count wraps round: a print then waits until
  // something reads, freezing the main loop (seen on the bench: over 2 minutes, 100+ GPS fixes dropped).
  Serial.setTxTimeoutMs(1);
#endif
  delay(300);
  DEBUG_PRINTLN("\n=== Track Rat - ES3C28P ===");
  DEBUG_PRINTF("PSRAM: %u bytes free\n", ESP.getFreePsram());

  Power::begin();
  gps.wake();  // a GPS the last sleep left in standby: it has the rest of setup() to start up
  Settings::begin();  // before the display: it starts the right way up, at the saved brightness
  Profiles::begin();  // the drivers and cars: before the screens that show them and the OBD adapter that may pick one
  Power::setBrightness(Settings::get().brightness);
  display.begin();
  display.startSplash();  // plays on the other core while the rest of setup runs
  display.drawBoot("Mounting storage...");
  Storage::begin();

  trackDB.begin();
  SessionLog::begin();
  input.begin();
  Imu::begin();  // on the touch controller's I2C bus, started just now

  display.drawBoot("Detecting GPS...");
  gps.begin();
  input.beginExtButton(gps.rxPinUsed(), gps.txPinUsed());  // the big button sits on a header pin the GPS may use
  CanObd::begin(gps.rxPinUsed(), gps.txPinUsed());  // (off unless CAN_ENABLED, and if the GPS is on its pins)
  applySettings();  // touch the right way up, Bluetooth if it's on, the time zone for file names
  ObdBle::begin();  // (nothing until an adapter has been chosen in Settings > Engine data)
  CameraBle::begin();  // (nothing until a camera has been paired in Settings > Camera)

  TrackDay::addBenchTrack();
  SessionLog::recoverUnfinished(trackDB);  // sessions cut short by a power cut, in the background
  // Re-timing from the Wi-Fi page's track editor: sessions timed again with the track's lines now
  WifiPortal::onRetime([](const String& trackId, const String& logName, String& error) {
    for (int i = 0; i < trackDB.count(); i++)
      if (trackDB.get(i)->id == trackId) return SessionLog::retime(*trackDB.get(i), logName, error);
    error = "No track called " + trackId;
    return false;
  });

  // Keep reading the GPS while the logo plays, so a fix found in the
  // meantime skips the waiting-for-GPS screen entirely.
  while (display.splashRunning()) {
    gps.update();
    delay(5);
  }
  display.finishSplash();
  SdUpdate::offer();
  gps.update();
  state = AppState::GpsWait;
  if (!Settings::get().setupDone) enterState(AppState::Setup);  // first boot: which way up, km/h or mph
  TrackDay::refreshWorldTracks();  // (the same screens as autoPickTrack: the world tracks near here come first)
  HomeScreen::autoPickTrack();  // (waits for the setup questions: it only acts on the GPS wait and menu screens)
  if (state == AppState::GpsWait) enterState(AppState::GpsWait);
  SleepControl::begin(millis());
}

// "2026-10-20T14:03:12.350Z": the GPS time at millis() `at`, from the latest fix
String gpsTimeAt(const GpsFix& f, uint32_t at) {
  int64_t ms = (int64_t)f.gpsTimeMs + (int32_t)(at - f.rxMillis);
  ms = ((ms % 86400000) + 86400000) % 86400000;  // (a start a moment before midnight keeps the fix's date)
  char b[32];
  snprintf(b, sizeof(b), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", f.year, f.month, f.day, (unsigned)(ms / 3600000), (unsigned)(ms / 60000 % 60),
           (unsigned)(ms / 1000 % 60), (unsigned)(ms % 1000));
  return String(b);
}

// The system clock from the GPS. The SD card's file dates come from it: never
// set, every file was dated 1980. Set at the first fix with a date, then again
// if it drifts by more than CLOCK_RESYNC_MS (checked once a minute)
static void setClockFromGps(const GpsFix& f) {
  static bool set = false;
  static uint32_t lastCheck = 0;
  const uint32_t now = millis();
  if (f.year < 2024 || (set && now - lastCheck < 60000)) return;
  lastCheck = now;
  const int64_t gpsMs = localtime_::unixMs(f.year, f.month, f.day, f.gpsTimeMs) + (int32_t)(now - f.rxMillis);
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  const int64_t clockMs = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
  if (set && llabs(gpsMs - clockMs) < CLOCK_RESYNC_MS) return;
  tv.tv_sec = (time_t)(gpsMs / 1000);
  tv.tv_usec = (suseconds_t)(gpsMs % 1000) * 1000;
  settimeofday(&tv, nullptr);
  if (!set) DEBUG_PRINTF("Clock set from GPS: %04u-%02u-%02u\n", f.year, f.month, f.day);
  set = true;
}

// Screens with nothing live on them (no clock, speed or map): menus, settings, summaries, lists, Ready, the
// Performance cards. The overheat warning, the SYNC flash and the power-off bar draw over any screen and stay live.
static const uint32_t STILL_FRAME_MS = 350;
static bool stillScreen(uint32_t now) {
  if (tempAlert.showing(now) || syncUntil || input.bootHeldMs() >= POWER_OFF_BAR_MS) return false;
  switch (state) {
    case AppState::Running:
    case AppState::FreeRoam:
      return false;
    case AppState::Performance:
      return PerformanceMode::still();
    default:
      return true;  // (a held arrow on Adjust redraws itself, see adjustHeld)
  }
}

void loop() {
  uint32_t now = millis();
  gps.update();
  Imu::poll();
  CanObd::poll();
  ObdBle::poll();
  CameraBle::poll();

  bool busy = false;  // something happened this pass: no nap at its end
  GpsFix fix;
  while (gps.takeFix(fix)) {
    busy = true;
    Imu::onFix(fix);  // its g for this fix, for the log and Bluetooth
#if BLE_ENABLED
    RaceBoxBle::sendFix(fix, gps);
#endif
    setClockFromGps(fix);
    CameraBle::onFix(fix, gps.hasFix());
    CameraBle::setRecording(cameraFollow.update(state == AppState::Running, Session::parked(), fix.speedKmh));
    cameraFix = fix;
    haveCameraFix = fix.year != 0;
    if (fix.speedKmh >= AUTO_SLEEP_MOVING_KMH) SleepControl::noteActivity(now);  // moving: never sleep
    if (state == AppState::Running) {
      Session::onFixRunning(fix);
    } else if (state == AppState::FreeRoam) {
      Session::onFixFreeRoam(fix);
    } else if (state == AppState::Performance) {
      PerformanceMode::onFix(fix);
    } else if ((state == AppState::TrackConfirm && autoStart) || (state == AppState::Summary && nextSessionArmed)) {
      // The next session only starts at the track: driving home from it isn't one
      const double d = state == AppState::Summary ? distanceToTrack(selectedIndex) : 0;
      if (d > TRACK_DETECTION_RADIUS_M || d < 0) {
        nextSessionArmed = false;
        DEBUG_PRINTLN("Left the track - no next session");
      } else if (driveOff.onFix(fix.speedKmh, fix.gpsTimeMs)) {
        DEBUG_PRINTLN("Driving off - starting a session");
        enterState(AppState::Running);
      }
    } else if (TRACK_LEARNING_ENABLED && state == AppState::Menu && HomeScreen::awayFromTracks()) HomeScreen::onFix(fix);
  }
  Session::replayTick();
  Session::noteCameraStart();
  AutocrossScreens::poll(now);
  if (!(state == AppState::Running && lapTimer.lapRunning())) CourseRuns::poll();  // the day's runs from the card, a little at a time, never mid-run
  Session::poll(now);

  LogWriter::poll();
  InputEvent ev = input.poll();
  if (ev != InputEvent::None && SleepControl::noteActivity(now)) ev = InputEvent::None;  // the press that wakes the screen does nothing else
  handleInput(ev);
  if (ev != InputEvent::None) busy = true, lastRender = 0;  // (most screens redraw themselves; the still ones rely on this)
  // BOOT held for POWER_OFF_HOLD_MS switches off, on any screen (render() shows the bar on the way, lighting a
  // dark screen for it), but not while firmware is being installed over Wi-Fi
  const uint32_t bootHeld = input.bootHeldMs();
  if (bootHeld >= POWER_OFF_BAR_MS && SleepControl::screenOff()) SleepControl::noteActivity(now);
  if (bootHeld >= POWER_OFF_HOLD_MS && !WifiPortal::busy()) SleepControl::powerOff();

  // A finger dragging the Settings list: the list follows it (and counts as using the board)
  const bool listDragged = SettingsScreens::followDrag(now);

  {  // The engine's temperatures against Settings > Coolant: the overheat warning
    float coolC = 0, oilC = 0;
    const bool coolKnown = EngineData::coolant(now, coolC), oilKnown = EngineData::oil(now, oilC);
    const bool wasShowing = tempAlert.showing(now);
    if (tempAlert.update(now, coolKnown, coolC, oilKnown, oilC, settings::tempLimits(Settings::get()))) {
      DEBUG_PRINTF("Overheat warning: %s %.0f C\n", tempAlert.current().oil ? "oil" : "coolant", tempAlert.current().c);
      SleepControl::noteActivity(now);  // the screen comes on for it
      lastRender = 0;
    } else if (wasShowing && !tempAlert.showing(now)) {
      lastRender = 0;  // back to the screen it covered
    }
  }

  Session::flushIfDue(now);
  TrackDay::refreshWorldTracks();
  HomeScreen::autoPickTrack();
  WifiPortal::loop(gps);
  if (WifiPortal::takeCodeChanged()) lastRender = 0;  // a change used the code: show the new one now
  {  // A new firmware that has run this long is trusted: it won't be rolled back
    static bool trusted = false;
    if (!trusted && now >= approllback::HEALTHY_AFTER_MS) {
      trusted = true;
      approllback::markGood();
    }
  }
  {  // A re-time finished: the PBs it may have rewritten are read from the card again
    static bool wasRetiming = false;
    const bool retiming = SessionLog::retiming();
    if (wasRetiming && !retiming) TrackDay::forgetPbs();
    wasRetiming = retiming;
  }
  if (Settings::changeCount() != settingsApplied) applySettings();  // e.g. changed on the Wi-Fi page
  // The adapter has reported the car's VIN: that car, from the next session on. applyVin waits while a session is
  // locked (Running, Free Roam, Performance), so its flash write never lands mid-session; and not while moving
  // either, where a few ms of flash would be a few ms of GPS bytes unread.
  if (!gps.hasFix() || gps.lastFix().speedKmh < 5.0f) Profiles::applyVin();

  SleepControl::poll(now);

  if (now - lastStatusLog > 5000) {
    lastStatusLog = now;
    char logInfo[128] = "";
    if (SessionLog::logging()) {
      snprintf(logInfo, sizeof(logInfo), " log=%lu fixes, %lu KB on card, %lu B queued, slowest write %lu ms",
               (unsigned long)SessionLog::fixesLogged(), (unsigned long)(LogWriter::bytesWritten() / 1024),
               (unsigned long)LogWriter::bufferedBytes(), (unsigned long)LogWriter::slowestWriteMs());
    }
    DEBUG_PRINTF("GPS: data=%s src=%s sats=%d %s=%.1f acc=%.1fm fix=%s rate=%.1fHz dropped=%lu%s%s%s\n",
                 gps.receivingData() ? "yes" : "NO", gps.usingUbx() ? "UBX" : "NMEA", gps.satellites(),
                 gps.usingUbx() ? "pdop" : "hdop", gps.hdop(), gps.accuracyM(), gps.hasFix() ? "yes" : "no",
                 gps.fixRateHz(), (unsigned long)gps.droppedFixCount(), gps.isSimulated() ? " (bench test)" : "",
                 logInfo, SessionLog::recovering() ? " (recovering old sessions)" : "");
  }

  // Dragging the list: draw as fast as the panel allows (about 30 a second) so it follows the finger.
  // A screen with nothing moving on it redraws only on input or a change (they set lastRender = 0), else every
  // STILL_FRAME_MS: 350, so the header's 700 ms REC / hot-coolant blink stays even.
  const uint32_t frameMs = listDragged ? 33 : stillScreen(now) ? STILL_FRAME_MS : 1000 / DISPLAY_FPS;
  if (!SleepControl::screenOff() && (lastRender == 0 || now - lastRender >= frameMs)) {
    lastRender = now;
    render();
    busy = true;
  }
  // Nothing waiting: let the idle task run instead of spinning (GPS bytes wait in the 16 KB UART buffer, about 2 KB/s)
  if (!busy) vTaskDelay(1);
}
