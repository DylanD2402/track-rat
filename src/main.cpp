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
#include "FirmwareUpdate.h"
#include "FreeRoam.h"
#include "GPSHandler.h"
#include "GeoMath.h"
#include "InputManager.h"
#include "LedBar.h"
#include "LapTimer.h"
#include "LogWriter.h"
#include "Power.h"
#include "PerfLog.h"
#include "PerfRun.h"
#include "RaceBoxBle.h"
#include "SessionLog.h"
#include "Settings.h"
#include "SettingsFlow.h"
#include "SimTrack.h"
#include "SleepTimer.h"
#include "Storage.h"
#include "TrackDatabase.h"
#include "TrackOrder.h"
#include "UiTheme.h"
#include "WifiPortal.h"

enum class AppState { GpsWait, Menu, TrackSelect, TrackConfirm, Running, Summary, GpsStatus, FreeRoam, FreeRoamSummary, Performance, Wifi,
                      Setup, SettingsScreen };
static const char* STATE_NAMES[] = {"GPS_WAIT", "MENU",      "TRACK_SELECT", "TRACK_CONFIRM",     "RUNNING",
                                    "SUMMARY",  "GPS_STATUS", "FREE_ROAM",   "FREE_ROAM_SUMMARY", "PERFORMANCE",
                                    "WIFI",     "SETUP",      "SETTINGS"};

static DisplayManager display;
static GPSHandler gps;
static InputManager input;
static LapTimer lapTimer;
static TrackDatabase trackDB;
static FreeRoam freeRoam;

static AppState state = AppState::GpsWait;
static AppState statusReturn = AppState::Menu;  // where the GPS Status screen goes back to
static AppState wifiReturn = AppState::Menu;    // where the Wi-Fi screen goes back to
static int selectedIndex = 0;
// Track list: trackDB indices, nearest first (see TrackOrder.h), and where in it we are
static std::vector<int> listOrder;
static int listPos = 0;
static bool listSorted = false;  // ordered by distance (false: no GPS fix when it was made)
static int menuHighlight = 0;
static bool autoPickDone = false;
static bool noTrackNearby = false;

// Lap-timing session
static bool settingLine = false;  // waiting for the driver to tap at the start/finish line
static String sessionPath;        // its summary (.json)
static String sessionStamp;
// Lap-timing and Free Roam sessions: every fix goes to a .vbo log (SD card)
static String sessionBase;        // "/sessions/<stamp>_<track>", shared by the .json and .vbo
static String sessionLog;         // the .vbo's file name, "" if not logging
static bool logPending = false;   // open the log at the next fix
static RunningOverlay overlay;
static bool showMap = false;  // timing screen: false = big clock, true = track map
static uint32_t lapOverlayUntil = 0;
static uint32_t splitOverlayUntil = 0;

// Layout check: after the first lap (or 2 km without crossing the line),
// is the driver on a different layout than the one picked? See checkLayout.
static const double LAYOUT_SEARCH_RADIUS_M = 3000;
static const float LINE_HEADING_MIN_KMH = 15;  // slower than this the GPS course is too rough to save with a line
static bool layoutChecked = false;
static std::vector<geo::XYf> drivenPath;  // before timing starts: metres from the track's reference point
static float drivenM = 0, nextLayoutCheckM = 0;
static uint32_t layoutNoticeUntil = 0;

// Free Roam
static String freeRoamPath;
static uint32_t freeRoamDurationMs = 0;
static bool freeRoamSaved = true;  // the Record road drives setting when the drive started

// Performance runs (acceleration and braking)
enum class PerfView { Live, Card, Best };
static perf::PerfRun perfRun;
static PerfView perfView = PerfView::Live;
static int perfCard = 0;            // run shown on the card screen
static bool perfRollout = PERF_ROLLOUT_DEFAULT;
static bool perfDemo = false;       // bench test: a simulated car launches and brakes on repeat
static String perfPath;
static String perfStamp;
static perf::SpeedUnit perfUnits() { return Settings::get().units == SpeedUnits::Mph ? perf::SpeedUnit::Mph : perf::SpeedUnit::Kmh; }

// Settings screen and the first-boot questions (the rules are in SettingsFlow.h)
static SettingsScreenState settingsUi;
static SetupState setupUi;
static uint32_t settingsApplied = 0;  // Settings::changeCount() when they were last applied

static uint32_t lastRender = 0;
static uint32_t lastStatusLog = 0;

static SleepTimer sleepTimer(AUTO_SLEEP_IDLE_MS, AUTO_SLEEP_DEEP_AFTER_MS);

static const Track& selectedTrack() { return *trackDB.get(selectedIndex); }

static double distanceToTrack(int index) {
  const Track* t = trackDB.get(index);
  if (!gps.hasFix() || t->bench) return -1;
  return geo::distanceM(gps.lastFix().lat, gps.lastFix().lon, t->refLat(), t->refLon());
}

// Orders the track list by distance from here. keepIndex >= 0 stays selected;
// otherwise the nearest track is.
static void sortTrackList(int keepIndex) {
  std::vector<trackorder::Item> items;
  std::vector<String> venues;  // venue ids, so layouts of one venue share a group number
  for (int i = 0; i < trackDB.count(); i++) {
    const Track* t = trackDB.get(i);
    int group = -1;
    if (t->layoutIndex >= 0) {
      auto it = std::find(venues.begin(), venues.end(), t->venueId);
      group = it - venues.begin();
      if (it == venues.end()) venues.push_back(t->venueId);
    }
    items.push_back({distanceToTrack(i), group, t->bench});
  }
  listOrder = trackorder::byDistance(items);
  listSorted = gps.hasFix();
  listPos = 0;
  for (size_t p = 0; keepIndex >= 0 && p < listOrder.size(); p++)
    if (listOrder[p] == keepIndex) listPos = p;
  if (!listOrder.empty()) selectedIndex = listOrder[listPos];
}

static void moveInTrackList(int step) {
  const int n = listOrder.size();
  if (n == 0) return;
  listPos = (listPos + step + n) % n;
  selectedIndex = listOrder[listPos];
}

static uint32_t minLapMs(const Track& t) { return t.bench ? SIM_MIN_LAP_MS : MIN_LAP_TIME_MS; }

static void beginLapTiming(const Track& t) {
  lapTimer.begin(t.startFinish, t.splits, minLapMs(t));
  overlay = RunningOverlay();
  lapOverlayUntil = splitOverlayUntil = 0;
}

// A new session's files: no name until it's needed (the first fix, or the
// first lap without an SD card), so it can use the GPS date.
static void startSessionFiles() {
  sessionBase = "";
  sessionLog = "";
  logPending = SessionLog::fullLogAvailable();
}

// The summary's path: next to the log, or a new name if there's no log.
static String summaryPath(const String& name) {
  if (sessionBase.length() == 0) sessionBase = SessionLog::newSessionBase(sessionStamp, name);
  return sessionBase.length() ? sessionBase + ".json" : String();
}

// First fix of a session: start its full-rate log. The header (and so any
// Circuit Tools timing line) is decided now: settingLine is final by then.
static void openSessionLog(const GpsFix& f) {
  logPending = false;
  if (sessionStamp.length() == 0) sessionStamp = gps.timestampString();
  const bool roam = state == AppState::FreeRoam;
  String base = SessionLog::newSessionBase(sessionStamp, roam ? String("freeroam") : selectedTrack().id);
  if (base.length() == 0) return;
  String path = base + ".vbo";
  bool ok = roam ? SessionLog::beginFreeRoamLog(path, sessionStamp, f)
                 : SessionLog::beginTrackLog(path, selectedTrack(), sessionStamp, !settingLine,
                                             minLapMs(selectedTrack()), f);
  if (ok) {
    sessionBase = base;
    sessionLog = SessionLog::fileName(path);
  }
}

static void recordFix(const GpsFix& f, uint16_t lap) {
  if (logPending) openSessionLog(f);
  SessionLog::logFix(f, lap);
}

// The lap-timing session ends: its finished summary, then close the log.
// With a log there's always a summary, laps or not, so the log isn't taken
// for an unfinished session at the next boot.
static void finishSessionFiles() {
  if (lapTimer.lapCount() > 0 || SessionLog::logging()) {
    if (sessionPath.length() == 0) sessionPath = summaryPath(selectedTrack().id);
    SessionLog::save(sessionPath, selectedTrack(), sessionStamp, lapTimer, sessionLog, true);
  }
  SessionLog::endLog();
  logPending = false;
}

// A lap's path with its points at least `spacing` metres apart.
static std::vector<geo::XYf> lapPath(const std::vector<LapTimer::TracePt>& trace, float spacing) {
  std::vector<geo::XYf> out;
  for (const LapTimer::TracePt& p : trace)
    if (out.empty() || hypotf(p.x - out.back().x, p.y - out.back().y) >= spacing) out.push_back({p.x, p.y});
  return out;
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
    startSessionFiles();
    layoutChecked = t.bench;
    drivenPath.clear();
    drivenM = 0;
    nextLayoutCheckM = 2000;
    if (settingLine) {
      DEBUG_PRINTF("Session at %s: waiting for the start/finish line to be marked\n", t.displayName().c_str());
    } else {
      static const char* FROM[] = {"either way", "set", "from the map"};
      DEBUG_PRINTF("Session started at %s (S/F %.6f, %.6f, gate +/-%.0f m, direction %.0f %s, %d splits)\n",
                   t.displayName().c_str(), t.startFinish.lat, t.startFinish.lon, t.startFinish.radius,
                   t.startFinish.heading, FROM[(int)t.startFinish.headingFrom], (int)t.splits.size());
    }
  }

  if (state == AppState::Running && next != AppState::Running) {
    finishSessionFiles();
    trackDB.keepGuessedHeadings(selectedIndex, lapTimer.startFinish(), lapTimer.splits());
    trackDB.saveMaps();  // the map from this session's best lap: saved now that nothing is being timed
    gps.setSimulated(false);
    settingLine = false;
  }

  if (next == AppState::Performance) {
    perfRun.clear();
    perfRun.setRollout(perfRollout);
    perfView = PerfView::Live;
    perfCard = 0;
    perfDemo = false;
    perfPath = "";
    perfStamp = gps.timestampString();
    DEBUG_PRINTLN("Performance mode started");
  }
  if (state == AppState::Performance && next != AppState::Performance) {
    if (perfDemo) gps.setSimulated(false);
    perfDemo = false;
  }

  if (next == AppState::FreeRoam) {
    freeRoam.start(millis());
    sessionStamp = gps.timestampString();
    startSessionFiles();
    freeRoamSaved = Settings::get().recordRoadDrives;  // Record road drives off: shown, but nothing saved
    if (!freeRoamSaved) logPending = false;
    DEBUG_PRINTF("Free roam started%s\n", freeRoamSaved ? "" : " (not saved: Record road drives is off)");
  }
  if (state == AppState::FreeRoam && next != AppState::FreeRoam) {
    freeRoamDurationMs = freeRoam.elapsedMs(millis());
    String path = freeRoamSaved ? summaryPath("freeroam") : String();
    freeRoamPath = path.length() && SessionLog::saveFreeRoam(path, sessionStamp, freeRoam, freeRoamDurationMs, sessionLog)
                       ? path
                       : String();
    SessionLog::endLog();
    logPending = false;
  }

  if (next == AppState::GpsStatus) statusReturn = state;

  // Opening the track list from the menu: nearest first. Coming back to it
  // (from a track's Ready screen or GPS status): stay on the same track.
  if (next == AppState::TrackSelect) {
    if (state == AppState::Menu || (int)listOrder.size() != trackDB.count()) sortTrackList(state == AppState::Menu ? -1 : selectedIndex);
    else for (size_t p = 0; p < listOrder.size(); p++) if (listOrder[p] == selectedIndex) listPos = p;
  }

  // Wi-Fi is on only while its screen is shown, never while timing.
  if (next == AppState::Wifi) {
    wifiReturn = state;
    WifiPortal::start();
  } else if (state == AppState::Wifi) {
    WifiPortal::stop();
  }

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
  // The direction of travel is saved with the line, so crossing it the other way doesn't count
  bool courseOk = f.courseValid && f.speedKmh >= LINE_HEADING_MIN_KMH;
  trackDB.setStartFinish(selectedIndex, f.lat, f.lon, courseOk ? f.courseDeg : -1);
  settingLine = false;
  beginLapTiming(selectedTrack());
  lapTimer.startLapNow(f.gpsTimeMs, f.rxMillis);
  layoutChecked = true;  // the driver just put the line here
  DEBUG_PRINTLN("Start/finish set - lap 1 started");
}

// Change to another layout mid-session (checkLayout found a better match).
// `map` is its map in metres from the current track's reference point.
static void switchLayout(int index, layout::Lines& map) {
  const Track& from = selectedTrack();
  const Track& to = *trackDB.get(index);
  // Timing carries on if the new layout shares the line it's been timed at
  const bool keepLaps =
      from.hasStartFinish && layout::sharesLine(from.startFinish, to.startFinish) && lapTimer.lapRunning();
  DEBUG_PRINTF("Layout: switching from '%s' to '%s'%s\n", from.displayName().c_str(), to.displayName().c_str(),
               keepLaps ? " (same start/finish, laps kept)" : " - new session");

  // Move the map to the new track's reference point (saves reading tracks.json again)
  geo::XY shift = geo::toLocal(to.refLat(), to.refLon(), from.refLat(), from.refLon());
  for (std::vector<geo::XYf>& line : map)
    for (geo::XYf& p : line) {
      p.x -= (float)shift.x;
      p.y -= (float)shift.y;
    }
  trackDB.keepGuessedHeadings(selectedIndex, lapTimer.startFinish(), lapTimer.splits());
  if (!keepLaps) finishSessionFiles();  // what was driven so far stays under the old layout
  selectedIndex = index;
  trackDB.setLoadedMap(index, std::move(map));
  const Track& t = selectedTrack();
  settingLine = false;  // only layouts with a line are switched to
  if (keepLaps) {
    lapTimer.changeGates(t.startFinish, t.splits);  // the laps so far were timed at this line too
    if (sessionBase.length()) {  // carry on in the same files, renamed for this layout
      String renamedBase = SessionLog::newSessionBase(sessionStamp, t.id);
      fs::FS* fs = Storage::sessionFS();
      String oldJson = sessionBase + ".json";
      String oldVbo = sessionBase + ".vbo";
      String newJson = renamedBase + ".json";
      String newVbo = renamedBase + ".vbo";
      // The JSON summary isn't held open, so it's renamed here. The .vbo is
      // open in the log writer, and FAT mustn't rename an open file, so the
      // writer task closes, renames and reopens it in order with its writes.
      bool renamedJson = fs && renamedBase.length() && (!fs->exists(oldJson) || fs->rename(oldJson, newJson));
      if (renamedJson) {
        bool renamedVbo = sessionLog.length() && SessionLog::logging() && LogWriter::rename(oldVbo, newVbo);
        if (sessionLog.length() && !renamedVbo)
          DEBUG_PRINTF("Layout: could not queue renaming %s - the log keeps its old name\n", oldVbo.c_str());
        sessionBase = renamedBase;
        sessionPath = newJson;
        if (renamedVbo) sessionLog = SessionLog::fileName(newVbo);
        SessionLog::save(sessionPath, t, sessionStamp, lapTimer, sessionLog, false);
      } else {
        DEBUG_PRINTF("Layout: could not rename %s to %s\n", oldJson.c_str(), newJson.c_str());
      }
    }
  } else {  // timing starts again at this layout's line, in new session files
    beginLapTiming(t);
    sessionStamp = gps.timestampString();
    sessionPath = "";
    startSessionFiles();
  }
  overlay.notice = "Layout: " + (t.layoutName.length() ? t.layoutName : t.name);
  layoutNoticeUntil = millis() + 6000;
}

// Compare the path driven with the map of each layout here (and any other
// track close by with a map). If another clearly fits better, and its
// start/finish was crossed the right way, switch to it. Returns true if it switched.
static bool checkLayout(const std::vector<geo::XYf>& path, bool afterLap) {
  const Track& cur = selectedTrack();
  std::vector<int> idx = trackDB.layoutCandidates(selectedIndex, LAYOUT_SEARCH_RADIUS_M);
  if (idx.empty() || path.size() < 10) return false;

  uint32_t startMs = millis();
  std::vector<layout::Lines> maps;
  if (!trackDB.readMaps(idx, cur.refLat(), cur.refLon(), maps)) return false;
  // Before any lap the current line hasn't been crossed, so it scores nothing
  float curScore = afterLap && !cur.map.empty() ? layout::score(path, cur.map).value() : 0;
  std::vector<layout::Candidate> cands;
  for (size_t k = 0; k < idx.size(); k++) {
    const Track& t = *trackDB.get(idx[k]);
    cands.push_back({geo::toLocal(t.startFinish.lat, t.startFinish.lon, cur.refLat(), cur.refLon()), t.startFinish,
                     &maps[k]});
  }
  float bestScore = 0;
  int k = layout::pickLayout(path, curScore, cands, &bestScore);
  DEBUG_PRINTF("Layout check (%s): '%s' scores %.2f, best other %.2f (%lu ms)%s\n",
               afterLap ? "first lap" : "no line crossed yet", cur.displayName().c_str(), curScore, bestScore,
               (unsigned long)(millis() - startMs), k >= 0 ? "" : " - staying");
  if (k < 0) return false;
  switchLayout(idx[k], maps[k]);
  return true;
}

// Before timing has started: keep the path, and every 2 km check whether
// the driver is on a layout whose line isn't the one picked.
static void trackDrivenPath(const GpsFix& f) {
  if (layoutChecked || lapTimer.lapRunning() || lapTimer.lapCount() > 0) return;
  if (f.speedKmh < 10) return;  // parked or crawling: GPS wander isn't distance driven
  const Track& t = selectedTrack();
  geo::XY p = geo::toLocal(f.lat, f.lon, t.refLat(), t.refLon());
  if (!drivenPath.empty()) {
    float d = hypotf((float)p.x - drivenPath.back().x, (float)p.y - drivenPath.back().y);
    if (d < 5.0f) return;
    drivenM += d;
  }
  if (drivenPath.size() < 4000) drivenPath.push_back({(float)p.x, (float)p.y});
  if (drivenM >= nextLayoutCheckM) {
    nextLayoutCheckM += 2000;
    if (checkLayout(drivenPath, false) || drivenPath.size() >= 4000) layoutChecked = true;
  }
}

// After a lap on a track with no map, save the lap's path as its map.
static void saveMapFromLap() {
  const Track& t = selectedTrack();
  if (t.hasMap || t.bench || lapTimer.bestPath().size() < 10) return;
  std::vector<geo::XYf> line = lapPath(lapTimer.bestPath(), 3.0f);
  line.push_back({0, 0});  // close the loop at the start/finish
  trackDB.setMap(selectedIndex, line);  // written to tracks.json when the session ends
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
      if (sessionPath.length() == 0) sessionPath = summaryPath(selectedTrack().id);
      if (SessionLog::save(sessionPath, selectedTrack(), sessionStamp, lapTimer, sessionLog, false)) {
        DEBUG_PRINTF("Saved %s:%s\n", Storage::sessionFSName(), sessionPath.c_str());
      }
      bool switched = false;
      if (!layoutChecked && lapTimer.lapCount() == 1) {  // first full lap: right layout?
        layoutChecked = true;
        switched = checkLayout(lapPath(lapTimer.bestPath(), 5.0f), true);
      }
      if (ev.isBest && !switched) {
        saveMapFromLap();
        LedBar::onBestLap();
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
      LedBar::onSplit();
      break;

    default:
      break;
  }
}

// Bench test for Performance mode: the simulated car launches, reaches 215 km/h, brakes and repeats.
static void startPerfDemo() {
  gps.setSimulated(true, SimMode::Launch);
  perfDemo = true;
  perfRun.clear();
  perfRun.setRollout(perfRollout);
  perfView = PerfView::Live;
  perfPath = "";
  perfStamp = gps.timestampString();
}

static void onPerfResult() {
  const perf::PerfResult& r = perfRun.results().back();
  if (!r.valid) {
    DEBUG_PRINTF("PERF %s: INVALID (GPS gap)\n",
                 r.kind == perf::RunKind::Brake ? "brake" : r.kind == perf::RunKind::Roll ? "roll" : "launch");
  } else if (r.kind == perf::RunKind::Brake) {
    DEBUG_PRINTF("PERF brake 100-0: %.1f m in %.2f s\n", r.brakeM, r.brakeS);
  } else if (r.kind == perf::RunKind::Roll) {
    DEBUG_PRINTF("PERF roll 100-200: %.2f s\n", r.t100to200);
  } else {
    DEBUG_PRINTF("PERF launch: 0-60 %.2f  0-100 %.2f  0-60mph %.2f  100-200 %.2f  1/8 %.2f @ %.0f  1/4 %.2f @ %.0f km/h%s\n",
                 r.t0to60, r.t0to100, r.t0to60mph, r.t100to200, r.et8, r.trap8Kmh, r.et4, r.trap4Kmh,
                 r.notLevel ? "  NOT LEVEL" : "");
  }
  if (perfPath.length() == 0) perfPath = PerfLog::newPath(perfStamp);
  if (PerfLog::save(perfPath, perfStamp, perfRun)) {
    DEBUG_PRINTF("Saved %s:%s\n", Storage::sessionFSName(), perfPath.c_str());
  }
  perfCard = (int)perfRun.results().size() - 1;
  perfView = PerfView::Card;
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
          layoutChecked = true;  // the driver asked for a new track: don't swap it for one nearby
        }
      }
      break;
    case 2:
      enterState(AppState::TrackSelect);
      break;
    case 3:  // Performance: works without a fix too, for the bench demo
      enterState(AppState::Performance);
      break;
    case 4:
      settingsUi = SettingsScreenState();
      enterState(AppState::SettingsScreen);
      break;
    case 5:
      enterState(AppState::Wifi);
      break;
  }
  lastRender = 0;
}

// Make the current settings take effect: the screen and touch the right way
// up, the backlight, Bluetooth, and the time zone for file names. Called at
// boot and whenever they change, from the screen or the Wi-Fi page.
static void applySettings() {
  const SettingsData s = Settings::get();
  settingsApplied = Settings::changeCount();
  display.setRotation(s.rotation);
  input.setUpsideDown(settings::upsideDown(s));
  Power::setBrightness(s.brightness);
#if BLE_ENABLED
  RaceBoxBle::setEnabled(s.bluetooth);
#endif
  gps.setUtcOffsetMinutes(s.utcOffsetMin);
  lastRender = 0;
}

static Key toKey(InputEvent ev) {
  switch (ev) {
    case InputEvent::TapLeft: return Key::Left;
    case InputEvent::TapCenter: return Key::Center;
    case InputEvent::TapRight: return Key::Right;
    case InputEvent::TapHeader: return Key::Header;
    case InputEvent::Hold: return Key::Hold;
    case InputEvent::BootShort: return Key::BootShort;
    case InputEvent::BootLong: return Key::BootLong;
    default: return Key::None;
  }
}

// The WI-FI button, in the right third of the menu and summary screens
static bool tappedWifi(InputEvent ev, int buttonTop) {
  return ev == InputEvent::TapRight && input.lastTapY() >= buttonTop - 4;
}

static void handleInput(InputEvent ev) {
  if (ev == InputEvent::None) return;

  switch (state) {
    case AppState::GpsWait:
      if (ev != InputEvent::Hold) {
        autoPickDone = true;  // the driver chose to go on without waiting
        enterState(AppState::Menu);
      }
      break;

    case AppState::Menu:
      if (ev == InputEvent::TapHeader) enterState(AppState::GpsStatus);
      else if ((ev == InputEvent::TapLeft || ev == InputEvent::TapCenter || ev == InputEvent::TapRight) &&
               input.lastTapY() >= DisplayManager::MENU_WIFI_Y &&
               input.lastTapY() < DisplayManager::MENU_WIFI_Y + DisplayManager::MENU_ROW_H) {
        chooseMenuItem(ev == InputEvent::TapLeft ? 3 : ev == InputEvent::TapCenter ? 4 : 5);  // Performance, Settings, Wi-Fi
      }
      else if (ev == InputEvent::TapLeft) chooseMenuItem(0);
      else if (ev == InputEvent::TapCenter) chooseMenuItem(1);
      else if (ev == InputEvent::TapRight) chooseMenuItem(2);
      else if (ev == InputEvent::BootShort) menuHighlight = (menuHighlight + 1) % 6;
      else if (ev == InputEvent::BootLong) chooseMenuItem(menuHighlight);
      lastRender = 0;
      break;

    case AppState::TrackSelect:
      if (ev == InputEvent::TapHeader) {
        enterState(AppState::GpsStatus);
        break;
      }
      if (ev == InputEvent::Hold) enterState(AppState::Menu);
      else if (ev == InputEvent::TapLeft) moveInTrackList(-1);
      else if (ev == InputEvent::TapRight || ev == InputEvent::BootShort) moveInTrackList(1);
      else if (ev == InputEvent::TapCenter || ev == InputEvent::BootLong) enterState(AppState::TrackConfirm);
      lastRender = 0;
      break;

    case AppState::TrackConfirm:
      if (ev == InputEvent::TapCenter || ev == InputEvent::BootLong) {
        enterState(AppState::Running);
      } else if (ev == InputEvent::TapRight && !selectedTrack().bench) {
        enterState(AppState::Running);
        settingLine = true;    // mark a new start/finish (replaces the old one)...
        layoutChecked = true;  // ...on the layout the driver picked, not one the check finds
      } else if (ev == InputEvent::TapLeft || ev == InputEvent::BootShort) {
        enterState(AppState::TrackSelect);
      } else if (ev == InputEvent::TapHeader) {  // next layout of this venue
        selectedIndex = trackDB.nextLayout(selectedIndex);
        lastRender = 0;
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

    case AppState::Performance: {
      // Taps are ignored while the car is moving, so a bump can't leave a run. (No fix: the speed is stale.)
      const bool busy = gps.hasFix() && (perfRun.state() == perf::PerfRun::State::Go || perfRun.speedKmh() > 5.0f);
      const int runs = (int)perfRun.results().size();
      if (ev == InputEvent::Hold || ev == InputEvent::BootLong) {
        enterState(AppState::Menu);
      } else if (ev == InputEvent::BootShort) {  // Live -> latest run -> session best -> Live
        if (perfView == PerfView::Live && !gps.hasFix() && !perfDemo) startPerfDemo();
        else if (perfView == PerfView::Live) perfView = runs ? PerfView::Card : PerfView::Best;
        else if (perfView == PerfView::Card) perfView = PerfView::Best;
        else perfView = PerfView::Live;
        if (perfView == PerfView::Card) perfCard = runs - 1;
      } else if (!busy && (ev == InputEvent::TapLeft || ev == InputEvent::TapCenter || ev == InputEvent::TapRight)) {
        if (perfView == PerfView::Live) {
          if (ev == InputEvent::TapLeft) {
            if (!gps.hasFix() && !perfDemo) {
              startPerfDemo();
            } else {
              perfRollout = !perfRollout;
              perfRun.setRollout(perfRollout);
            }
          } else if (ev == InputEvent::TapCenter) {
            perfView = runs ? PerfView::Card : PerfView::Best;
            perfCard = runs - 1;
          } else {
            enterState(AppState::Menu);
          }
        } else if (perfView == PerfView::Card) {
          if (ev == InputEvent::TapLeft && runs) perfCard = (perfCard + runs - 1) % runs;
          else if (ev == InputEvent::TapCenter) perfView = PerfView::Best;
          else if (ev == InputEvent::TapRight) perfView = PerfView::Live;
        } else {
          if (ev == InputEvent::TapLeft && runs) perfView = PerfView::Card, perfCard = runs - 1;
          else if (ev == InputEvent::TapRight) perfView = PerfView::Live;
        }
      }
      lastRender = 0;
      break;
    }

    case AppState::Summary:  // back to this track's Ready screen, to go out again
      if (tappedWifi(ev, DisplayManager::WIFI_BUTTON_Y) || ev == InputEvent::BootLong) enterState(AppState::Wifi);
      else if (ev != InputEvent::Hold) enterState(AppState::TrackConfirm);
      break;

    case AppState::FreeRoamSummary:
      if (tappedWifi(ev, DisplayManager::WIFI_BUTTON_Y) || ev == InputEvent::BootLong) enterState(AppState::Wifi);
      else if (ev != InputEvent::Hold) enterState(AppState::Menu);
      break;

    case AppState::Wifi:  // any tap or press turns Wi-Fi off, except mid firmware update
      if (ev != InputEvent::Hold && !WifiPortal::busy()) enterState(wifiReturn);
      break;

    case AppState::GpsStatus:
      if (ev != InputEvent::Hold) enterState(statusReturn);
      break;

    case AppState::SettingsScreen: {
      SettingsData s = Settings::get();
      SettingsScreenState::Result r = settingsUi.handle(toKey(ev), s);
      if (r == SettingsScreenState::Result::Changed) {
        Settings::set(s);
        applySettings();
      } else if (r == SettingsScreenState::Result::Exit) {
        enterState(AppState::Menu);
      }
      lastRender = 0;
      break;
    }

    case AppState::Setup: {
      SettingsData s = Settings::get();
      if (setupUi.handle(toKey(ev), s)) {
        Settings::set(s);
        applySettings();  // the screen turns round as soon as the way up is chosen
        if (s.setupDone) {
          DEBUG_PRINTF("Setup: %s, %s\n", settings::upsideDown(s) ? "upside down" : "normal",
                       s.units == SpeedUnits::Mph ? "mph" : "km/h");
          enterState(AppState::GpsWait);
        }
      }
      lastRender = 0;
      break;
    }
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
      // The list opened before the GPS had a fix: sort it once a fix arrives
      // (to the nearest track, unless the driver has already moved along it).
      if (!listSorted && gps.hasFix()) sortTrackList(listPos == 0 ? -1 : selectedIndex);
      display.drawTrackSelect(selectedTrack(), listPos, (int)listOrder.size(), distanceToTrack(selectedIndex),
                              gps, input.touchAvailable());
      break;
    case AppState::TrackConfirm:
      display.drawTrackConfirm(selectedTrack(), distanceToTrack(selectedIndex), gps.hasFix(),
                               input.touchAvailable(), trackDB.layoutNumber(selectedIndex),
                               trackDB.layoutCount(selectedIndex));
      break;
    case AppState::Running:
      if (settingLine) {
        display.drawSetLine(selectedTrack(), gps, input.touchHeldMs());
        break;
      }
      if (overlay.showLap && now > lapOverlayUntil) overlay.showLap = false;
      if (overlay.splitIndex >= 0 && now > splitOverlayUntil) overlay.splitIndex = -1;
      if (overlay.notice.length() && now > layoutNoticeUntil) overlay.notice = "";
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
    case AppState::Performance:
      if (perfRun.state() == perf::PerfRun::State::Go) perfView = PerfView::Live;  // a new run is on
      if (perfView == PerfView::Live) {
        const char* left = (!gps.hasFix() && !perfDemo) ? "Demo" : "1 ft";
        display.drawPerfLive(perfRun, gps, perfUnits(), left, input.touchHeldMs(), input.touchAvailable());
      } else {
        display.drawPerfCard(perfRun, perfView == PerfView::Card ? perfCard : -1, perfUnits(), input.touchAvailable());
      }
      break;
    case AppState::SettingsScreen:
      display.drawSettings(Settings::get(), settingsUi.item, input.touchAvailable(),
                           gps.hasFix() ? (int64_t)settings::localTimeOfDayMs(gps.lastFix().gpsTimeMs,
                                                                               Settings::get().utcOffsetMin)
                                        : -1);
      break;
    case AppState::Setup:
      display.drawSetup(setupUi.step, setupUi.highlight, input.touchAvailable());
      break;
    case AppState::Wifi:
      display.drawWifi(WifiPortal::active(), WifiPortal::networkName(), WifiPortal::address(),
                       WifiPortal::clientCount(), WifiPortal::uploadCodeValue(), WifiPortal::updateProgress(),
                       WifiPortal::restarting(), input.touchAvailable());
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
  sim.startFinish = {SIM_SF_LAT, SIM_SF_LON, (float)DEFAULT_GATE_HALF_WIDTH_M, 0, HeadingFrom::Set};  // north
  sim.hasStartFinish = true;
  sim.centerLat = SIM_SF_LAT;
  sim.centerLon = SIM_SF_LON;
  sim.splits.push_back({SIM_SPLIT_LAT, SIM_SPLIT_LON, (float)DEFAULT_GATE_HALF_WIDTH_M, 180, HeadingFrom::Set});
  std::vector<geo::XYf> outline;  // the circle, for the map screen
  for (int i = 0; i <= 72; i++) {
    float a = i * 2 * M_PI / 72;
    outline.push_back({(float)(SIM_RADIUS_M * (cos(a) - 1.0)), (float)(SIM_RADIUS_M * sin(a))});
  }
  sim.map.push_back(outline);
  sim.hasMap = true;
  trackDB.add(sim);
}

// ---------------------------------------------------------------------------
// Sleep and battery
// ---------------------------------------------------------------------------

static const uint32_t SLEEP_LOG_WAIT_MS = 3000;  // an SD write can take a few hundred ms

// Close and save whatever is open, so power can go. Anything else that must be
// flushed before sleep (e.g. an open log file) belongs here too.
static void saveForSleep() {
  if (state == AppState::Running) enterState(AppState::Summary);  // saves the laps so far
  else if (state == AppState::FreeRoam) enterState(AppState::FreeRoamSummary);
  SessionLog::endLog();
  LogWriter::poll();
  // The writer task only closes the file when it next runs, and deep sleep would stop it first.
  if (!LogWriter::waitIdle(SLEEP_LOG_WAIT_MS)) DEBUG_PRINTLN("Log: card write still pending at sleep");
}

// Something happened (a touch, a press, the car moving). Returns true if the screen was off.
static bool noteActivity(uint32_t now) {
  if (!sleepTimer.activity(now)) return false;
  display.setBacklight(true);
  lastRender = 0;
  return true;
}

static void lowBatterySleep() {
  DEBUG_PRINTF("Battery low (%d mV): saving and sleeping\n", Power::batteryMillivolts());
  saveForSleep();
  display.setBacklight(true);
  display.drawMessage("BATTERY LOW", "Saved. Going to sleep.", "Charge it, then touch or press BOOT", ui::C_YELLOW);
  delay(BATTERY_LOW_SCREEN_MS);
  display.powerDown();
  Power::deepSleep();
}

// ---------------------------------------------------------------------------
// Firmware update from the SD card
// ---------------------------------------------------------------------------

static void drawInstallProgress(size_t done, size_t total) {
  static int lastPct = -1;
  int pct = total ? (int)(done * 100 / total) : 0;
  if (pct == lastPct) return;
  lastPct = pct;
  display.drawFirmwareProgress(pct);
}

// If /trackrat.bin is on the card, offer to install it: tap the centre or hold
// BOOT to confirm; anything else (or no answer) skips.
static void offerFirmwareUpdate() {
  FirmwareUpdate::Offer offer;
  if (!FirmwareUpdate::find(offer)) return;

  if (offer.check != firmware::Check::Ok) {
    display.drawMessage("FIRMWARE FILE SKIPPED", "Can't use trackrat.bin", firmware::describe(offer.check), ui::C_RED);
    delay(4000);
    return;
  }

  char modified[24] = "";
  if (offer.modified > 0) {
    struct tm t;
    gmtime_r(&offer.modified, &t);  // FAT stores local time, so no zone conversion
    strftime(modified, sizeof(modified), "%Y-%m-%d %H:%M", &t);
  }
  display.drawFirmwarePrompt(modified, offer.size, input.touchAvailable());
  bool install = false;
  uint32_t start = millis();
  while (millis() - start < FIRMWARE_PROMPT_TIMEOUT_MS) {
    gps.update();  // keep draining the GPS while waiting
    InputEvent ev = input.poll();
    if (ev == InputEvent::None) {
      delay(5);
      continue;
    }
    install = ev == InputEvent::TapCenter || ev == InputEvent::BootLong;
    break;
  }
  if (!install) {
    DEBUG_PRINTLN("Firmware: update skipped");
    return;
  }

  DEBUG_PRINTLN("Firmware: installing");
  const char* error = FirmwareUpdate::install(offer, drawInstallProgress);
  if (error) {
    DEBUG_PRINTF("Firmware: install failed: %s\n", error);
    display.drawMessage("UPDATE FAILED", error, "The old firmware is unchanged", ui::C_RED);
    delay(5000);
    return;
  }
  display.drawMessage("UPDATE INSTALLED", "Restarting…", "", ui::C_GREEN);
  delay(1500);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  Serial.setTxTimeoutMs(0);  // never block when no USB host is attached (car power)
#endif
  delay(300);
  DEBUG_PRINTLN("\n=== Track Rat - ES3C28P ===");
  DEBUG_PRINTF("PSRAM: %u bytes free\n", ESP.getFreePsram());

  Power::begin();
  Settings::begin();  // before the display: it starts the right way up, at the saved brightness
  Power::setBrightness(Settings::get().brightness);
  display.begin();
  display.startSplash();  // plays on the other core while the rest of setup runs
  display.drawBoot("Mounting storage...");
  Storage::begin();

  trackDB.begin();
  SessionLog::begin();
  input.begin();

  display.drawBoot("Detecting GPS...");
  gps.begin();
  input.beginExtButton(gps.rxPinUsed(), gps.txPinUsed());  // big button and LEDs sit on header pins the GPS may use
  LedBar::begin(gps.rxPinUsed(), gps.txPinUsed());
  applySettings();  // touch the right way up, Bluetooth if it's on, the time zone for file names

  addBenchTrack();
  SessionLog::recoverUnfinished(trackDB);  // sessions cut short by a power cut, in the background

  // Keep reading the GPS while the logo plays, so a fix found in the
  // meantime skips the waiting-for-GPS screen entirely.
  while (display.splashRunning()) {
    gps.update();
    delay(5);
  }
  display.finishSplash();
  offerFirmwareUpdate();
  gps.update();
  state = AppState::GpsWait;
  if (!Settings::get().setupDone) enterState(AppState::Setup);  // first boot: which way up, km/h or mph
  autoPickTrack();  // (waits for the setup questions: it only acts on the GPS wait and menu screens)
  if (state == AppState::GpsWait) enterState(AppState::GpsWait);
  sleepTimer.begin(millis());
}

void loop() {
  uint32_t now = millis();
  gps.update();

  GpsFix fix;
  while (gps.takeFix(fix)) {
#if BLE_ENABLED
    RaceBoxBle::sendFix(fix, gps);
#endif
    if (fix.speedKmh >= AUTO_SLEEP_MOVING_KMH) noteActivity(now);  // moving: never sleep
    if (state == AppState::Running) {
      recordFix(fix, !settingLine && lapTimer.lapRunning() ? lapTimer.currentLapNumber() : 0);
      if (!settingLine) {
        LapEvents evs = lapTimer.onFix(fix.lat, fix.lon, fix.speedKmh, fix.gpsTimeMs, fix.rxMillis);
        for (const LapEvent& ev : evs) onLapEvent(ev);
      }
      trackDrivenPath(fix);
    } else if (state == AppState::FreeRoam) {
      recordFix(fix, 0);
      freeRoam.onFix(fix.lat, fix.lon, fix.speedKmh);
    } else if (state == AppState::Performance) {
      if (perfRun.onFix(fix.gpsTimeMs, fix.speedKmh, fix.altM) > 0) onPerfResult();
    }
  }

  LogWriter::poll();
  InputEvent ev = input.poll();
  if (ev != InputEvent::None && noteActivity(now)) ev = InputEvent::None;  // the press that wakes the screen does nothing else
  handleInput(ev);

  bool timing = state == AppState::Running && !settingLine && lapTimer.lapRunning();
  int32_t liveDelta = lapTimer.liveDeltaMs();
  LedBar::update(timing, gps.hasFix() && liveDelta != NO_DELTA, liveDelta);

  autoPickTrack();
  WifiPortal::loop(gps);
  if (Settings::changeCount() != settingsApplied) applySettings();  // e.g. changed on the Wi-Fi page

  Power::update(now);
  display.setBatteryPercent(Power::batteryPercent());
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
      display.powerDown();
      Power::deepSleep();
      break;
    default:
      break;
  }
#endif

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

  if (!sleepTimer.screenOff() && (lastRender == 0 || now - lastRender >= 1000 / DISPLAY_FPS)) {
    lastRender = now;
    render();
  }
}
