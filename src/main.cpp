/**
 * Track Rat - GPS lap timer
 *
 * Board: LCDWiki ES3C28P (ESP32-S3 N16R8, 2.8" ILI9341V touch display, SD slot)
 * GPS:   Beitian BE-252Q (UART connector or expansion header, found at boot)
 *
 * Flow: logo -> GPS fix -> a track within 5 km? start a session on it (timing
 *       begins at the start/finish line) : menu (Free Roam / New Track /
 *       Track list). Tracks without a start/finish line get one by tapping
 *       the screen while crossing it.
 */

#include <Arduino.h>
#include "Config.h"
#include "DisplayManager.h"
#include "FreeRoam.h"
#include "GPSHandler.h"
#include "GeoMath.h"
#include "InputManager.h"
#include "LapTimer.h"
#include "RaceBoxBle.h"
#include "SessionLog.h"
#include "SimTrack.h"
#include "Storage.h"
#include "TrackDatabase.h"

enum class AppState { GpsWait, Menu, TrackSelect, TrackConfirm, Running, Summary, GpsStatus, FreeRoam, FreeRoamSummary };
static const char* STATE_NAMES[] = {"GPS_WAIT", "MENU",      "TRACK_SELECT", "TRACK_CONFIRM",     "RUNNING",
                                    "SUMMARY",  "GPS_STATUS", "FREE_ROAM",   "FREE_ROAM_SUMMARY"};

static DisplayManager display;
static GPSHandler gps;
static InputManager input;
static LapTimer lapTimer;
static TrackDatabase trackDB;
static FreeRoam freeRoam;

static AppState state = AppState::GpsWait;
static AppState statusReturn = AppState::Menu;  // where the GPS Status screen goes back to
static int selectedIndex = 0;
static int menuHighlight = 0;
static bool autoPickDone = false;
static bool noTrackNearby = false;

// Lap-timing session
static bool settingLine = false;  // waiting for the driver to tap at the start/finish line
static String sessionPath;
static String sessionStamp;
static RunningOverlay overlay;
static bool showMap = false;  // timing screen: false = big clock, true = track map
static uint32_t lapOverlayUntil = 0;
static uint32_t splitOverlayUntil = 0;

// Free Roam
static String freeRoamPath;
static uint32_t freeRoamDurationMs = 0;

static uint32_t lastRender = 0;
static uint32_t lastStatusLog = 0;

static const Track& selectedTrack() { return *trackDB.get(selectedIndex); }

static double distanceToTrack(int index) {
  const Track* t = trackDB.get(index);
  if (!gps.hasFix() || t->bench) return -1;
  return geo::distanceM(gps.lastFix().lat, gps.lastFix().lon, t->refLat(), t->refLon());
}

static void beginLapTiming(const Track& t) {
  lapTimer.begin(t.startFinish, t.splits, t.bench ? SIM_MIN_LAP_MS : MIN_LAP_TIME_MS);
  overlay = RunningOverlay();
  lapOverlayUntil = splitOverlayUntil = 0;
}

static void enterState(AppState next) {
  DEBUG_PRINTF("State: %s -> %s\n", STATE_NAMES[(int)state], STATE_NAMES[(int)next]);

  if (next == AppState::Running) {
    const Track& t = selectedTrack();
    gps.setSimulated(t.bench);
    trackDB.loadMap(selectedIndex);
    if (!t.hasStartFinish) settingLine = true;
    beginLapTiming(t);
    sessionStamp = gps.timestampString();
    sessionPath = "";
    if (settingLine) {
      DEBUG_PRINTF("Session at %s: waiting for the start/finish line to be marked\n", t.name.c_str());
    } else {
      DEBUG_PRINTF("Session started at %s (S/F %.6f, %.6f, gate +/-%.0f m, %d splits)\n", t.name.c_str(),
                   t.startFinish.lat, t.startFinish.lon, t.startFinish.radius, (int)t.splits.size());
    }
  }

  if (state == AppState::Running && next == AppState::Summary && lapTimer.lapCount() > 0) {
    if (sessionPath.length() == 0) sessionPath = SessionLog::newSessionPath(selectedTrack(), sessionStamp);
    SessionLog::save(sessionPath, selectedTrack(), sessionStamp, lapTimer);
  }
  if (state == AppState::Running && next != AppState::Running) {
    gps.setSimulated(false);
    settingLine = false;
  }

  if (next == AppState::FreeRoam) {
    freeRoam.start(millis());
    sessionStamp = gps.timestampString();
    DEBUG_PRINTLN("Free roam started");
  }
  if (state == AppState::FreeRoam && next == AppState::FreeRoamSummary) {
    freeRoamDurationMs = freeRoam.elapsedMs(millis());
    freeRoamPath = SessionLog::saveFreeRoam(sessionStamp, freeRoam, freeRoamDurationMs);
  }

  if (next == AppState::GpsStatus) statusReturn = state;
  state = next;
  lastRender = 0;  // redraw now
}

// The driver tapped while crossing the start/finish line: save it and start lap 1.
static void markStartFinish() {
  if (!gps.hasFix()) {
    DEBUG_PRINTLN("Start/finish: no GPS fix - not set");
    return;
  }
  const GpsFix& f = gps.lastFix();
  trackDB.setStartFinish(selectedIndex, f.lat, f.lon);
  settingLine = false;
  beginLapTiming(selectedTrack());
  lapTimer.startLapNow(f.gpsTimeMs, f.rxMillis);
  DEBUG_PRINTLN("Start/finish set - lap 1 started");
}

// After a lap on a track with no map, save the lap's path as its map.
static void saveMapFromLap() {
  const Track& t = selectedTrack();
  if (t.hasMap || t.bench || lapTimer.bestPath().size() < 10) return;
  std::vector<geo::XYf> line;
  const float spacing = 3.0f;
  for (const LapTimer::TracePt& p : lapTimer.bestPath()) {
    if (line.empty() || hypotf(p.x - line.back().x, p.y - line.back().y) >= spacing) line.push_back({p.x, p.y});
  }
  line.push_back({0, 0});  // close the loop at the start/finish
  trackDB.saveMap(selectedIndex, line);
}

static void onLapEvent(const LapEvent& ev) {
  uint32_t now = millis();
  switch (ev.type) {
    case LapEventType::LapStarted:
      DEBUG_PRINTF("Lap %d started\n", lapTimer.currentLapNumber());
      break;

    case LapEventType::LapCompleted: {
      const Lap* lap = lapTimer.lastLap();
      DEBUG_PRINTF("LAP %d: %s  (delta %s)%s  max %.0f km/h\n", lap->number, formatLapTime(lap->timeMs).c_str(),
                   formatDelta(ev.deltaMs).c_str(), ev.isBest ? "  BEST" : "", lap->maxSpeedKmh);
      overlay.showLap = true;
      overlay.lapNumber = lap->number;
      overlay.lapMs = lap->timeMs;
      overlay.lapIsBest = ev.isBest;
      lapOverlayUntil = now + 4000;

      // Save after every lap so a power cut doesn't lose the session.
      if (sessionPath.length() == 0) sessionPath = SessionLog::newSessionPath(selectedTrack(), sessionStamp);
      if (SessionLog::save(sessionPath, selectedTrack(), sessionStamp, lapTimer)) {
        DEBUG_PRINTF("Saved %s:%s\n", Storage::sessionFSName(), sessionPath.c_str());
      }
      if (ev.isBest) saveMapFromLap();
      break;
    }

    case LapEventType::SplitCrossed:
      DEBUG_PRINTF("Split %d: %s  (delta %s)\n", ev.splitIndex + 1, formatLapTime(ev.timeMs).c_str(),
                   formatDelta(ev.deltaMs).c_str());
      overlay.splitIndex = ev.splitIndex;
      overlay.splitMs = ev.timeMs;
      overlay.deltaMs = ev.deltaMs;
      splitOverlayUntil = now + 5000;
      break;

    default:
      break;
  }
}

static void chooseMenuItem(int item) {
  switch (item) {
    case 0:  // Free Roam
      if (gps.hasFix()) enterState(AppState::FreeRoam);
      break;
    case 1:  // New Track: created here, start/finish marked on the first pass
      if (gps.hasFix()) {
        int idx = trackDB.createTrack(gps.lastFix().lat, gps.lastFix().lon);
        if (idx >= 0) {
          selectedIndex = idx;
          enterState(AppState::Running);
        }
      }
      break;
    case 2:
      enterState(AppState::TrackSelect);
      break;
  }
  lastRender = 0;
}

static void handleInput(InputEvent ev) {
  if (ev == InputEvent::None) return;
  int count = trackDB.count();

  switch (state) {
    case AppState::GpsWait:
      if (ev != InputEvent::Hold) {
        autoPickDone = true;  // the driver chose to go on without waiting
        enterState(AppState::Menu);
      }
      break;

    case AppState::Menu:
      if (ev == InputEvent::TapHeader) enterState(AppState::GpsStatus);
      else if (ev == InputEvent::TapLeft) chooseMenuItem(0);
      else if (ev == InputEvent::TapCenter) chooseMenuItem(1);
      else if (ev == InputEvent::TapRight) chooseMenuItem(2);
      else if (ev == InputEvent::BootShort) menuHighlight = (menuHighlight + 1) % 3;
      else if (ev == InputEvent::BootLong) chooseMenuItem(menuHighlight);
      lastRender = 0;
      break;

    case AppState::TrackSelect:
      if (ev == InputEvent::TapHeader) {
        enterState(AppState::GpsStatus);
        break;
      }
      if (ev == InputEvent::Hold) enterState(AppState::Menu);
      else if (ev == InputEvent::TapLeft) selectedIndex = (selectedIndex + count - 1) % count;
      else if (ev == InputEvent::TapRight || ev == InputEvent::BootShort) selectedIndex = (selectedIndex + 1) % count;
      else if (ev == InputEvent::TapCenter || ev == InputEvent::BootLong) enterState(AppState::TrackConfirm);
      lastRender = 0;
      break;

    case AppState::TrackConfirm:
      if (ev == InputEvent::TapCenter || ev == InputEvent::BootLong) {
        enterState(AppState::Running);
      } else if (ev == InputEvent::TapRight && !selectedTrack().bench) {
        enterState(AppState::Running);
        settingLine = true;  // mark a new start/finish (replaces the old one)
      } else if (ev == InputEvent::TapLeft || ev == InputEvent::BootShort) {
        enterState(AppState::TrackSelect);
      }
      break;

    case AppState::Running:
      if (ev == InputEvent::Hold || ev == InputEvent::BootLong) enterState(AppState::Summary);
      else if (settingLine) markStartFinish();  // any tap marks the line
      else {                                    // any tap or short BOOT press: switch view
        showMap = !showMap;
        lastRender = 0;
      }
      break;

    case AppState::FreeRoam:
      if (ev == InputEvent::Hold || ev == InputEvent::BootLong) enterState(AppState::FreeRoamSummary);
      break;

    case AppState::Summary:  // back to this track's Ready screen, to go out again
      if (ev != InputEvent::Hold) enterState(AppState::TrackConfirm);
      break;

    case AppState::FreeRoamSummary:
      if (ev != InputEvent::Hold) enterState(AppState::Menu);
      break;

    case AppState::GpsStatus:
      if (ev != InputEvent::Hold) enterState(statusReturn);
      break;
  }
}

static void render() {
  uint32_t now = millis();
  switch (state) {
    case AppState::GpsWait:
      display.drawGpsWait(gps, trackDB.count(), trackDB.source(), input.touchAvailable(), false);
      break;
    case AppState::GpsStatus:
      display.drawGpsWait(gps, trackDB.count(), trackDB.source(), input.touchAvailable(), true);
      break;
    case AppState::Menu:
      display.drawMenu(input.touchAvailable() ? -1 : menuHighlight, gps, noTrackNearby, input.touchAvailable());
      break;
    case AppState::TrackSelect:
      display.drawTrackSelect(selectedTrack(), selectedIndex, trackDB.count(), distanceToTrack(selectedIndex),
                              gps, input.touchAvailable());
      break;
    case AppState::TrackConfirm:
      display.drawTrackConfirm(selectedTrack(), distanceToTrack(selectedIndex), gps.hasFix(),
                               input.touchAvailable());
      break;
    case AppState::Running:
      if (settingLine) {
        display.drawSetLine(selectedTrack(), gps, input.touchHeldMs());
        break;
      }
      if (overlay.showLap && now > lapOverlayUntil) overlay.showLap = false;
      if (overlay.splitIndex >= 0 && now > splitOverlayUntil) overlay.splitIndex = -1;
      if (showMap) display.drawRunningMap(selectedTrack(), lapTimer, gps, overlay, input.touchHeldMs());
      else display.drawRunning(selectedTrack(), lapTimer, gps, overlay, input.touchHeldMs());
      break;
    case AppState::Summary:
      display.drawSummary(selectedTrack(), lapTimer, sessionPath);
      break;
    case AppState::FreeRoam:
      display.drawFreeRoam(freeRoam, gps, input.touchHeldMs());
      break;
    case AppState::FreeRoamSummary:
      display.drawFreeRoamSummary(freeRoam, freeRoamDurationMs, freeRoamPath);
      break;
  }
}

// Once the GPS has a fix: a track within range starts a session on it
// straight away; otherwise show the menu.
static void autoPickTrack() {
  if (autoPickDone || !gps.hasFix()) return;
  if (state != AppState::GpsWait && state != AppState::Menu) return;
  autoPickDone = true;
  double dist;
  int nearest = trackDB.findNearest(gps.lastFix().lat, gps.lastFix().lon, TRACK_DETECTION_RADIUS_M, &dist);
  if (nearest >= 0) {
    selectedIndex = nearest;
    DEBUG_PRINTF("Nearest track: %s (%.0f m) - starting a session\n", selectedTrack().name.c_str(), dist);
    enterState(AppState::Running);
  } else {
    noTrackNearby = true;
    DEBUG_PRINTF("No track within %.0f km\n", TRACK_DETECTION_RADIUS_M / 1000.0);
    if (state == AppState::GpsWait) enterState(AppState::Menu);
  }
}

// Bench test: picking this track runs a simulated car around this circle
// (see SimTrack.h and GPSHandler::simulateStep) instead of using the GPS.
static void addBenchTrack() {
  Track sim;
  sim.id = "bench";
  sim.name = "Bench Test";
  sim.location = "Simulated car - no GPS needed";
  sim.bench = true;
  sim.length = 2 * M_PI * SIM_RADIUS_M;
  sim.startFinish = {SIM_SF_LAT, SIM_SF_LON, (float)DEFAULT_GATE_HALF_WIDTH_M};
  sim.hasStartFinish = true;
  sim.centerLat = SIM_SF_LAT;
  sim.centerLon = SIM_SF_LON;
  sim.splits.push_back({SIM_SPLIT_LAT, SIM_SPLIT_LON, (float)DEFAULT_GATE_HALF_WIDTH_M});
  std::vector<geo::XYf> outline;  // the circle, for the map screen
  for (int i = 0; i <= 72; i++) {
    float a = i * 2 * M_PI / 72;
    outline.push_back({(float)(SIM_RADIUS_M * (cos(a) - 1.0)), (float)(SIM_RADIUS_M * sin(a))});
  }
  sim.map.push_back(outline);
  sim.hasMap = true;
  trackDB.add(sim);
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  Serial.setTxTimeoutMs(0);  // never block when no USB host is attached (car power)
#endif
  delay(300);
  DEBUG_PRINTLN("\n=== Track Rat - ES3C28P ===");
  DEBUG_PRINTF("PSRAM: %u bytes free\n", ESP.getFreePsram());

  display.begin();
  display.startSplash();  // plays on the other core while the rest of setup runs
  display.drawBoot("Mounting storage...");
  Storage::begin();

  trackDB.begin();
  input.begin();

  display.drawBoot("Detecting GPS...");
  gps.begin();
#if BLE_ENABLED
  RaceBoxBle::begin();
#endif

  addBenchTrack();

  // Keep reading the GPS while the logo plays, so a fix found in the
  // meantime skips the waiting-for-GPS screen entirely.
  while (display.splashRunning()) {
    gps.update();
    delay(5);
  }
  display.finishSplash();
  gps.update();
  state = AppState::GpsWait;
  autoPickTrack();
  if (state == AppState::GpsWait) enterState(AppState::GpsWait);
}

void loop() {
  uint32_t now = millis();
  gps.update();

  GpsFix fix;
  while (gps.takeFix(fix)) {
#if BLE_ENABLED
    RaceBoxBle::sendFix(fix, gps);
#endif
    if (state == AppState::Running && !settingLine) {
      onLapEvent(lapTimer.onFix(fix.lat, fix.lon, fix.speedKmh, fix.gpsTimeMs, fix.rxMillis));
    } else if (state == AppState::FreeRoam) {
      freeRoam.onFix(fix.lat, fix.lon, fix.speedKmh);
    }
  }

  handleInput(input.poll());

  autoPickTrack();

  if (now - lastStatusLog > 5000) {
    lastStatusLog = now;
    DEBUG_PRINTF("GPS: data=%s sats=%d hdop=%.1f fix=%s rate=%.1fHz dropped=%lu%s\n",
                 gps.receivingData() ? "yes" : "NO", gps.satellites(), gps.hdop(), gps.hasFix() ? "yes" : "no",
                 gps.fixRateHz(), (unsigned long)gps.droppedFixCount(), gps.isSimulated() ? " (bench test)" : "");
  }

  if (lastRender == 0 || now - lastRender >= 1000 / DISPLAY_FPS) {
    lastRender = now;
    render();
  }
}
