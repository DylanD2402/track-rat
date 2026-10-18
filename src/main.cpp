/**
 * Track Rat - GPS lap timer
 *
 * Board: LCDWiki ES3C28P (ESP32-S3 N16R8, 2.8" ILI9341V touch display, SD slot)
 * GPS:   Beitian BE-252Q on UART1 (GPIO 14 RX / GPIO 21 TX)
 *
 * Flow: wait for GPS -> pick track (auto: nearest within 5 km) -> confirm ->
 *       timing (laps start/end on start/finish crossings) -> summary
 */

#include <Arduino.h>
#include "Config.h"
#include "DisplayManager.h"
#include "GPSHandler.h"
#include "GeoMath.h"
#include "InputManager.h"
#include "LapTimer.h"
#include "RaceBoxBle.h"
#include "SessionLog.h"
#include "SimTrack.h"
#include "Storage.h"
#include "TrackDatabase.h"

enum class AppState { GpsWait, TrackSelect, TrackConfirm, Running, Summary, GpsStatus };

static DisplayManager display;
static GPSHandler gps;
static InputManager input;
static LapTimer lapTimer;
static TrackDatabase trackDB;

static AppState state = AppState::GpsWait;
static int selectedIndex = 0;
static bool nearestSuggested = false;

static String sessionPath;
static String sessionStamp;
static RunningOverlay overlay;
static bool showMap = false;  // timing screen: false = big clock, true = track map
static uint32_t lapOverlayUntil = 0;
static uint32_t splitOverlayUntil = 0;

static uint32_t lastRender = 0;
static uint32_t lastStatusLog = 0;

static const Track& selectedTrack() { return *trackDB.get(selectedIndex); }

static double distanceToTrack(int index) {
  const Track* t = trackDB.get(index);
  if (!gps.hasFix() || t->bench) return -1;
  return geo::distanceM(gps.lastFix().lat, gps.lastFix().lon, t->startFinish.lat, t->startFinish.lon);
}

static void enterState(AppState next) {
  static const char* names[] = {"GPS_WAIT", "TRACK_SELECT", "TRACK_CONFIRM", "RUNNING", "SUMMARY", "GPS_STATUS"};
  DEBUG_PRINTF("State: %s -> %s\n", names[(int)state], names[(int)next]);

  if (next == AppState::Running) {
    const Track& t = selectedTrack();
    gps.setSimulated(t.bench);
    lapTimer.begin(t.startFinish, t.splits, t.bench ? SIM_MIN_LAP_MS : MIN_LAP_TIME_MS);
    overlay = RunningOverlay();
    lapOverlayUntil = splitOverlayUntil = 0;
    sessionStamp = gps.timestampString();
    sessionPath = "";
    DEBUG_PRINTF("Session started at %s (S/F %.6f, %.6f, gate +/-%.0f m, %d splits)\n", t.name.c_str(),
                 t.startFinish.lat, t.startFinish.lon, t.startFinish.radius, (int)t.splits.size());
  }

  if (state == AppState::Running && next == AppState::Summary && lapTimer.lapCount() > 0) {
    if (sessionPath.length() == 0) sessionPath = SessionLog::newSessionPath(selectedTrack(), sessionStamp);
    SessionLog::save(sessionPath, selectedTrack(), sessionStamp, lapTimer);
  }

  if (state == AppState::Running && next != AppState::Running) gps.setSimulated(false);

  state = next;
  lastRender = 0;  // redraw now
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

static void handleInput(InputEvent ev) {
  if (ev == InputEvent::None) return;
  int count = trackDB.count();

  switch (state) {
    case AppState::GpsWait:
      if (ev != InputEvent::Hold) enterState(AppState::TrackSelect);
      break;

    case AppState::TrackSelect:
      if (ev == InputEvent::TapHeader) {
        enterState(AppState::GpsStatus);
        break;
      }
      if (ev == InputEvent::TapLeft) selectedIndex = (selectedIndex + count - 1) % count;
      else if (ev == InputEvent::TapRight || ev == InputEvent::BootShort) selectedIndex = (selectedIndex + 1) % count;
      else if (ev == InputEvent::TapCenter || ev == InputEvent::BootLong) enterState(AppState::TrackConfirm);
      lastRender = 0;
      break;

    case AppState::TrackConfirm:
      if (ev == InputEvent::TapCenter || ev == InputEvent::BootLong) enterState(AppState::Running);
      else if (ev == InputEvent::TapLeft || ev == InputEvent::BootShort) enterState(AppState::TrackSelect);
      break;

    case AppState::Running:
      if (ev == InputEvent::Hold || ev == InputEvent::BootLong) enterState(AppState::Summary);
      else if (ev != InputEvent::None) {  // any tap or short BOOT press: switch view
        showMap = !showMap;
        lastRender = 0;
      }
      break;

    case AppState::Summary:
      if (ev != InputEvent::Hold) enterState(AppState::TrackSelect);
      break;

    case AppState::GpsStatus:
      if (ev != InputEvent::Hold) enterState(AppState::TrackSelect);
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
    case AppState::TrackSelect:
      display.drawTrackSelect(selectedTrack(), selectedIndex, trackDB.count(), distanceToTrack(selectedIndex),
                              gps, input.touchAvailable());
      break;
    case AppState::TrackConfirm:
      display.drawTrackConfirm(selectedTrack(), distanceToTrack(selectedIndex), gps.hasFix(),
                               input.touchAvailable());
      break;
    case AppState::Running:
      if (overlay.showLap && now > lapOverlayUntil) overlay.showLap = false;
      if (overlay.splitIndex >= 0 && now > splitOverlayUntil) overlay.splitIndex = -1;
      if (showMap) display.drawRunningMap(selectedTrack(), lapTimer, gps, overlay, input.touchHeldMs());
      else display.drawRunning(selectedTrack(), lapTimer, gps, overlay, input.touchHeldMs());
      break;
    case AppState::Summary:
      display.drawSummary(selectedTrack(), lapTimer, sessionPath);
      break;
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
  sim.splits.push_back({SIM_SPLIT_LAT, SIM_SPLIT_LON, (float)DEFAULT_GATE_HALF_WIDTH_M});
  std::vector<geo::XYf> outline;  // the circle, for the map screen
  for (int i = 0; i <= 72; i++) {
    float a = i * 2 * M_PI / 72;
    outline.push_back({(float)(SIM_RADIUS_M * (cos(a) - 1.0)), (float)(SIM_RADIUS_M * sin(a))});
  }
  sim.map.push_back(outline);
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

  enterState(AppState::GpsWait);
}

void loop() {
  uint32_t now = millis();
  gps.update();

  GpsFix fix;
  if (gps.takeFix(fix)) {
#if BLE_ENABLED
    RaceBoxBle::sendFix(fix, gps);
#endif
    if (state == AppState::Running) {
      onLapEvent(lapTimer.onFix(fix.lat, fix.lon, fix.speedKmh, fix.gpsTimeMs, fix.rxMillis));
    }
  }

  handleInput(input.poll());

  // Automatic track pick once the GPS has a fix
  if (gps.hasFix() && !nearestSuggested && (state == AppState::GpsWait || state == AppState::TrackSelect)) {
    nearestSuggested = true;
    double dist;
    int nearest = trackDB.findNearest(gps.lastFix().lat, gps.lastFix().lon, TRACK_DETECTION_RADIUS_M, &dist);
    if (nearest >= 0) {
      selectedIndex = nearest;
      DEBUG_PRINTF("Nearest track: %s (%.0f m)\n", selectedTrack().name.c_str(), dist);
      if (state == AppState::GpsWait) enterState(AppState::TrackConfirm);
    } else if (state == AppState::GpsWait) {
      DEBUG_PRINTLN("No track within range - choose one manually");
      enterState(AppState::TrackSelect);
    }
  }

  if (now - lastStatusLog > 5000) {
    lastStatusLog = now;
    DEBUG_PRINTF("GPS: data=%s sats=%d hdop=%.1f fix=%s rate=%.1fHz%s\n", gps.receivingData() ? "yes" : "NO",
                 gps.satellites(), gps.hdop(), gps.hasFix() ? "yes" : "no", gps.fixRateHz(),
                 gps.isSimulated() ? " (bench test)" : "");
  }

  if (lastRender == 0 || now - lastRender >= 1000 / DISPLAY_FPS) {
    lastRender = now;
    render();
  }
}
