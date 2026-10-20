/**
 * Track Rat - GPS lap timer
 *
 * Board: LCDWiki ES3C28P (ESP32-S3 N16R8, 2.8" ILI9341V touch display, SD slot)
 * GPS:   Beitian BE-252Q (UART connector or expansion header, found at boot)
 *
 * Flow: logo -> GPS fix -> a track within 5 km? already moving: start a session
 *       on it (timing begins at the start/finish line); parked: its Ready
 *       screen, which starts the session when the car drives off (Menu is
 *       one button away) : menu (Free Roam / New Track / Track list). Tracks
 *       without a start/finish line get one by tapping the screen while
 *       crossing it.
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
#include "AutoStart.h"
#include "BestLapStore.h"
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
#include "Sectors.h"
#include "SessionLog.h"
#include "Settings.h"
#include "SettingsFlow.h"
#include "SimTrack.h"
#include "SleepTimer.h"
#include "Storage.h"
#include "TrackDatabase.h"
#include "TrackLearn.h"
#include "TrackOrder.h"
#include "TimingScreen.h"
#include "SituationScreen.h"
#include "UiTheme.h"
#include "WifiPortal.h"

enum class AppState { GpsWait, Menu, TrackSelect, TrackConfirm, Running, Summary, GpsStatus, FreeRoam, FreeRoamSummary, Performance, Wifi,
                      Setup, SettingsScreen, NewTrack };
static const char* STATE_NAMES[] = {"GPS_WAIT", "MENU",      "TRACK_SELECT", "TRACK_CONFIRM",     "RUNNING",
                                    "SUMMARY",  "GPS_STATUS", "FREE_ROAM",   "FREE_ROAM_SUMMARY", "PERFORMANCE",
                                    "WIFI",     "SETUP",      "SETTINGS",     "NEW_TRACK"};
static int newTrackHighlight = 0;  // the NEW TRACK screen: 0 = Circuit, 1 = Course (BOOT button)

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

// The Ready screen: where it was entered from decides whether its left button
// is "Back" (to the track list) or "Menu". Auto-start: a track was picked at
// boot with the car parked, so the screen starts the session by itself once
// the car drives off (see AutoStart.h). It ends when the screen is left.
static bool readyFromList = false;
static bool autoStart = false;
static DriveOffDetector driveOff;

// Lap-timing session
static bool settingLine = false;  // waiting for the driver to tap at the start/finish line
static bool sectorsTried = false; // the track has corner sectors, or this session tried to make them
static int sessionNumber = 0;     // "Session N" of the day at this track
static PitWatch pitWatch;         // parked: in the pits (see AutoStart.h)
static bool pitView = false;      // parked, showing the debrief (or the summary without one); taps page on, then back to timing
static int pitPage = 0;           // the debrief's page
static DebriefView debriefView;   // worked out when the car parks
static bool nextSessionArmed = false;  // a session ended by itself: the summary starts the next one on driving off
static String sessionPath;        // its summary (.json)
static String sessionStamp;
// Lap-timing and Free Roam sessions: every fix goes to a .vbo log (SD card)
static String sessionBase;        // "/sessions/<stamp>_<track>", shared by the .json and .vbo
static String sessionLog;         // the .vbo's file name, "" if not logging
static bool logPending = false;   // open the log at the next fix
static RunningOverlay overlay;
static bool showMap = false;  // timing screen: false = big clock, true = track map
static uint32_t lapOverlayUntil = 0;
static timing::DeltaTrend deltaTrend;  // the timing screen's trend bar
static situation::SlowWatch slowWatch;  // slow or in the pit lane: the slow screen
static bool inPitLane = false;           // the last fix was in the pit lane (tracks.json marks it)
static uint32_t sessionStartMs = 0;      // millis() when the session started: its clock
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

// Tracks that learn themselves (TrackLearn.h): Free Roam watches for a loop
// driven twice. The fixes of the last LEARN_WINDOW_MS are kept (PSRAM) so a
// learned track's session can time both loops: they're fed through the lap
// timer a few at a time (replaying), with new fixes queued behind them.
static learn::LoopFinder loopFinder;
static learn::FixRing<GpsFix> recentFixes;
static size_t replayPos = 0;      // the next kept fix to time
static bool replaying = false;
static DriveOffDetector roamDriveOff;  // the menu with no track nearby: driving off starts Free Roam

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

static uint32_t minLapMs(const Track& t) { return t.bench ? SIM_MIN_LAP_MS : t.stage ? STAGE_MIN_RUN_MS : MIN_LAP_TIME_MS; }

// Best laps (BestLap.h): the PB and today's best at the track being timed,
// without their paths (the reference lap's path is in the lap timer)
static bestlap::Record pbRecord, dayRecord;
// The track's stats as they were when the session started (best sector
// times ever, the session before): see BestLapStore::Stats
static BestLapStore::Stats trackStats;
// PBs for the track list and Ready screen, read from the card once per track
struct PbInfo {
  bool read = false;
  uint32_t ms = 0, date = 0;
};
static std::vector<PbInfo> pbCache;

static void personalBestFor(int index, uint32_t& ms, uint32_t& date) {
  if ((int)pbCache.size() != trackDB.count()) pbCache.assign(trackDB.count(), PbInfo());
  PbInfo& c = pbCache[index];
  if (!c.read) {
    c.read = true;
    const Track* t = trackDB.get(index);
    bestlap::Record r;
    if (t->hasStartFinish && BestLapStore::load(t->id, BestLapStore::Kind::Ever, r, false) &&
        bestlap::sameLine(r, t->startFinish.lat, t->startFinish.lon)) {
      c.ms = r.lapMs;
      c.date = r.date;
    }
  }
  ms = c.ms;
  date = c.date;
}

// One stored best lap of the selected track, if it was timed at its line as it is now.
static bool loadBestLap(BestLapStore::Kind kind, bestlap::Record& r, bool withPath) {
  const Track& t = selectedTrack();
  if (BestLapStore::load(t.id, kind, r, withPath) && bestlap::sameLine(r, t.startFinish.lat, t.startFinish.lon))
    return true;
  r = bestlap::Record();
  return false;
}

static void updateSectorBests();

// At the start of timing: the PB and today's best, and the one the Compare
// with setting picks becomes the live delta's reference until this session
// has a faster lap.
static void loadBestLaps() {
  const Track& t = selectedTrack();
  pbRecord = dayRecord = bestlap::Record();
  lapTimer.clearReference();
  if (!t.hasStartFinish || settingLine) return;
  loadBestLap(BestLapStore::Kind::Ever, pbRecord, false);
  loadBestLap(BestLapStore::Kind::Day, dayRecord, false);
  BestLapStore::loadStats(t.id, trackStats);
  updateSectorBests();
  const uint32_t today = bestlap::dateFromStamp(sessionStamp.c_str());
  const bestlap::Ref ref = bestlap::pickReference(Settings::get().compareTo, pbRecord, dayRecord, today);
  bestlap::Record r;
  if (ref != bestlap::Ref::None &&
      loadBestLap(ref == bestlap::Ref::Day ? BestLapStore::Kind::Day : BestLapStore::Kind::Ever, r, true)) {
    std::vector<LapTimer::TracePt> path;
    path.reserve(r.path.size());
    for (const bestlap::Point& p : r.path) path.push_back({p.x, p.y, p.t, p.speedKmh});
    lapTimer.setReference(std::move(path), r.lapMs);
    DEBUG_PRINTF("Best lap: comparing with %s %s (%s)\n", ref == bestlap::Ref::Day ? "today's best" : "the PB",
                 formatLapTime(r.lapMs).c_str(), bestlap::dateText(r.date).c_str());
  }
}

// A new best lap of the session: is it the PB, or today's best? Saves it
// if so, and says whether it's a PB (and in isDayBest, today's best). Tagged laps never count.
static bool saveBestLaps(const Lap& lap, bool& isDayBest) {
  isDayBest = false;
  const Track& t = selectedTrack();
  if (lap.tags || !t.hasStartFinish || settingLine) return false;
  const uint32_t today = bestlap::dateFromStamp(sessionStamp.c_str());
  const bool pb = bestlap::beats(lap.timeMs, lap.tags, pbRecord);
  const bool dayBest = bestlap::beatsDay(lap.timeMs, lap.tags, dayRecord, today);
  isDayBest = dayBest;
  if (!pb && !dayBest) return false;

  bestlap::Record r;  // the lap timer's best path is this lap's
  r.lapMs = lap.timeMs;
  r.date = today;
  r.sfLat = lapTimer.startFinish().lat;
  r.sfLon = lapTimer.startFinish().lon;
  r.path.reserve(lapTimer.bestPath().size());
  for (const LapTimer::TracePt& p : lapTimer.bestPath()) r.path.push_back({p.x, p.y, p.t, p.speedKmh});
  if (pb) BestLapStore::save(t.id, BestLapStore::Kind::Ever, r);
  if (dayBest) BestLapStore::save(t.id, BestLapStore::Kind::Day, r);
  r.path.clear();
  if (pb) pbRecord = r;
  if (dayBest) dayRecord = r;
  // This session's best is now the reference: a new PB beats the stored lap, and comparing with
  // today's best, until now that was the PB (no lap today yet)
  if (pb || (dayBest && Settings::get().compareTo == CompareTo::Day)) lapTimer.clearReference();
  if (selectedIndex < (int)pbCache.size()) pbCache[selectedIndex].read = false;
  DEBUG_PRINTF("Best lap: %s%s%s\n", pb ? "new PB" : "", pb && dayBest ? ", " : "", dayBest ? "best of the day" : "");
  return pb;
}

static void beginLapTiming(const Track& t) {
  lapTimer.begin(t.startFinish, t.splits, minLapMs(t), t.sectors);
  if (t.stage) lapTimer.setStage(t.finish, t.hasFinish);  // point to point: runs end at the finish
  sectorsTried = !t.sectors.empty();
  overlay = RunningOverlay();
  lapOverlayUntil = splitOverlayUntil = 0;
  deltaTrend.clear();
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
                 : SessionLog::beginTrackLog(path, selectedTrack(), sessionStamp, sessionNumber, !settingLine,
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
// This session's laps as the debrief reads them (Debrief.h).
static debrief::Result analyseSession() {
  std::vector<debrief::LapIn> laps;
  laps.reserve(lapTimer.lapCount());
  for (size_t i = 0; i < lapTimer.lapCount(); i++) {
    const Lap& l = lapTimer.lap(i);
    laps.push_back({l.number, l.timeMs, l.tags, l.sectorMs});
  }
  return debrief::analyse(laps, lapTimer.sectorGates().empty() ? 0 : lapTimer.sectorGates().size() + 1);
}

// The best sector times ever: the stored ones (if set through these gates) with this session's.
static std::vector<int32_t> allTimeSectorBests(const debrief::Result& r) {
  const bool same = debrief::sameGates(trackStats.sectorGates, lapTimer.sectorGates());
  return debrief::mergeBest(same ? trackStats.sectorBestMs : std::vector<int32_t>(), r);
}

// The best in each sector today: the stored ones (if from today, through these gates) with this session's.
static std::vector<int32_t> todaySectorBests(const debrief::Result& r) {
  const bool same = debrief::sameGates(trackStats.sectorGates, lapTimer.sectorGates()) &&
                    trackStats.dayDate == bestlap::dateFromStamp(sessionStamp.c_str());
  return debrief::mergeBest(same ? trackStats.daySectorBestMs : std::vector<int32_t>(), r);
}

// The timing screen's sector strip compares with these (TimingScreen.h). Worked out
// when timing starts and after each lap, not every frame.
static void updateSectorBests() {
  overlay.sectorBestEver.clear();
  overlay.sectorBestDay.clear();
  if (lapTimer.sectorGates().empty()) return;
  const debrief::Result r = analyseSession();
  overlay.sectorBestEver = allTimeSectorBests(r);
  overlay.sectorBestDay = todaySectorBests(r);
}

// When the car parks: the theoretical best, the three sectors that cost the
// most, and a hint for each from the two laps' speed through it.
static void buildDebrief() {
  DebriefView d;
  d.r = analyseSession();
  d.session = sessionNumber;
  d.lastSession = trackStats.lastSession;
  d.lastBestMs = trackStats.lastBestMs;
  d.lastToday = trackStats.lastDate && trackStats.lastDate == bestlap::dateFromStamp(sessionStamp.c_str());
  if (d.r.valid) {
    d.allTimeTheoreticalMs = debrief::sumOf(allTimeSectorBests(d.r));
    // Hints need the best lap's own path: bestPath() is it unless that lap was tagged later
    if (lapTimer.bestPathLapIndex() == d.r.bestIndex) {
      const std::vector<int32_t>& gates = lapTimer.lap(d.r.bestIndex).sectorMs;
      const bool mph = Settings::get().units == SpeedUnits::Mph;
      for (size_t k = 0; k < d.r.top.size(); k++) {
        const int s = d.r.top[k];
        // The kept path must be from the lap the debrief names (a lap tagged slow later isn't)
        if (lapTimer.sectorBestMs(s) != d.r.best[s].ms) continue;
        const int32_t from = s == 0 ? 0 : gates[s - 1];
        const int32_t to = s < (int)gates.size() ? gates[s] : (int32_t)d.r.bestMs;
        debrief::Trace mine = debrief::slice(lapTimer.bestPath(), from, to);
        debrief::Trace best = debrief::slice(lapTimer.sectorBestPath(s), INT32_MIN, INT32_MAX);
        d.hints[k] = debrief::hintText(debrief::hint(mine, best), mph);
      }
    }
    DEBUG_PRINTF("Debrief: best %s, theoretical %s (%s s on the table), %d laps used\n",
                 formatLapTime(d.r.bestMs).c_str(), formatLapTime(d.r.theoreticalMs).c_str(),
                 debrief::seconds(d.r.bestMs - d.r.theoreticalMs).c_str(), d.r.lapsUsed);
    for (size_t k = 0; k < d.r.top.size(); k++)
      DEBUG_PRINTF("  T%d: %s s, best on lap %u%s%s\n", d.r.top[k] + 1, debrief::seconds(d.r.gaps[d.r.top[k]]).c_str(),
                   (unsigned)d.r.best[d.r.top[k]].lap, d.hints[k].size() ? ": " : "", d.hints[k].c_str());
  }
  debriefView = d;
}

// The track's stats with this session in: the best sector times ever, and
// this as the last session (for the next one's "quicker than session N").
static void saveTrackStats() {
  const Track& t = selectedTrack();
  const Lap* best = lapTimer.bestLap();
  if (!best || !t.hasStartFinish) return;
  BestLapStore::Stats s = trackStats;
  debrief::Result r = analyseSession();
  if (r.valid) {
    s.sectorBestMs = allTimeSectorBests(r);
    s.daySectorBestMs = todaySectorBests(r);
    s.dayDate = bestlap::dateFromStamp(sessionStamp.c_str());
    s.sectorGates.clear();
    for (const TrackPoint& g : lapTimer.sectorGates()) s.sectorGates.push_back({g.lat, g.lon});
  }
  s.lastSession = sessionNumber;
  s.lastDate = bestlap::dateFromStamp(sessionStamp.c_str());
  s.lastBestMs = best->timeMs;
  BestLapStore::saveStats(t.id, s);
}

static void finishSessionFiles() {
  if (lapTimer.lapCount() > 0 || SessionLog::logging()) {
    if (sessionPath.length() == 0) sessionPath = summaryPath(selectedTrack().id);
    SessionLog::save(sessionPath, selectedTrack(), sessionStamp, sessionNumber, lapTimer, sessionLog, true);
  }
  if (!settingLine) saveTrackStats();
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
    sessionNumber = Settings::nextSessionNumber(t.id.c_str(), sessionStamp.c_str());
    sessionPath = "";
    startSessionFiles();
    loadBestLaps();
    pitWatch.reset();
    pitView = false;
    slowWatch.reset();
    inPitLane = false;
    sessionStartMs = millis();
    layoutChecked = t.bench || t.stage;  // a stage is its own course
    drivenPath.clear();
    drivenM = 0;
    nextLayoutCheckM = 2000;
    if (settingLine) {
      DEBUG_PRINTF("Session %d at %s: waiting for the start/finish line to be marked\n", sessionNumber, t.displayName().c_str());
    } else {
      static const char* FROM[] = {"either way", "set", "from the map"};
      DEBUG_PRINTF("Session %d started at %s (S/F %.6f, %.6f, gate +/-%.0f m, direction %.0f %s, %d splits)\n",
                   sessionNumber, t.displayName().c_str(), t.startFinish.lat, t.startFinish.lon, t.startFinish.radius,
                   t.startFinish.heading, FROM[(int)t.startFinish.headingFrom], (int)t.splits.size());
    }
  }

  if (state == AppState::Running && next != AppState::Running) {
    replaying = false;
    recentFixes.release();
    finishSessionFiles();
    trackDB.keepGuessedHeadings(selectedIndex, lapTimer.startFinish(), lapTimer.splits());
    trackDB.saveMaps();  // the map from this session's best lap: saved now that nothing is being timed
    gps.setSimulated(false);
    settingLine = false;
    pitView = false;
  }
  if (state == AppState::Summary) nextSessionArmed = false;  // set again by whoever ends a session by itself

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
#if TRACK_LEARNING_ENABLED
    loopFinder.reset();
    {  // 20 Hz, with a minute to spare: about 1.2 MB. Without the room a learned track times laps from then on.
      const size_t n = LEARN_WINDOW_MS / 1000 * 21 + 60 * 21;
      if (ESP.getMaxAllocPsram() > n * sizeof(GpsFix) + 512 * 1024) recentFixes.begin(n);
      else DEBUG_PRINTLN("Free roam: no room to keep fixes - a learned track is timed from when it's found");
    }
#endif
  }
  if (state == AppState::FreeRoam && next != AppState::FreeRoam) {
    freeRoamDurationMs = freeRoam.elapsedMs(millis());
    String path = freeRoamSaved ? summaryPath("freeroam") : String();
    freeRoamPath = path.length() && SessionLog::saveFreeRoam(path, sessionStamp, freeRoam, freeRoamDurationMs, sessionLog)
                       ? path
                       : String();
    SessionLog::endLog();
    logPending = false;
    if (next != AppState::Running) recentFixes.release();  // (Running: a learned track, timed from them)
  }
  if (next == AppState::Menu) roamDriveOff.reset();

  if (next == AppState::GpsStatus) statusReturn = state;

  if (next == AppState::TrackConfirm && state != AppState::TrackConfirm) readyFromList = state == AppState::TrackSelect;
  if (state == AppState::TrackConfirm && next != AppState::TrackConfirm) autoStart = false;

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
// NEW TRACK, then Circuit: created here, start/finish marked on the first pass.
// Course (autocross, hill climb, sprint): a stage named with the date, starting
// here (where the car waits to start), finish tapped on the first run.
static void createNewTrack(bool course) {
  if (!gps.hasFix()) return enterState(AppState::Menu);
  const GpsFix& f = gps.lastFix();
  int idx = course ? trackDB.createCourse(f.lat, f.lon, gps.timestampString()) : trackDB.createTrack(f.lat, f.lon);
  if (idx < 0) return enterState(AppState::Menu);
  selectedIndex = idx;
  enterState(AppState::Running);
  layoutChecked = true;  // the driver asked for a new track: don't swap it for one nearby
  if (course) {
    overlay.notice = "Start here: the clock starts as you go";
    layoutNoticeUntil = millis() + 10000;
  }
}

static void onLapEvent(const LapEvent& ev);

// A stage with no finish yet: the driver tapped crossing it. Saves it and ends the run there.
static void markFinish() {
  if (!gps.hasFix()) return;
  const GpsFix& f = gps.lastFix();
  const bool courseOk = f.courseValid && f.speedKmh >= LINE_HEADING_MIN_KMH;
  LapEvents evs = lapTimer.finishRunNow(f.lat, f.lon, f.gpsTimeMs);
  trackDB.setFinish(selectedIndex, f.lat, f.lon, courseOk ? f.courseDeg : -1);
  lapTimer.setStage(selectedTrack().finish, true);
  overlay.notice = "";
  for (const LapEvent& ev : evs) onLapEvent(ev);
  DEBUG_PRINTLN("Finish set - the run ends here");
}

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
  if (selectedIndex < (int)pbCache.size()) pbCache[selectedIndex].read = false;  // the PB may not count at the new line
  loadBestLaps();  // a line put back where it was keeps its best laps
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
    lapTimer.setSectors(t.sectors);
    sectorsTried = !t.sectors.empty();
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
        // A note typed on the Wi-Fi page meanwhile (Logbook.h) goes with it
        String oldNote = sessionBase + "_note.json";
        if (fs->exists(oldNote)) fs->rename(oldNote, renamedBase + "_note.json");
        sessionBase = renamedBase;
        sessionPath = newJson;
        if (renamedVbo) sessionLog = SessionLog::fileName(newVbo);
        SessionLog::save(sessionPath, t, sessionStamp, sessionNumber, lapTimer, sessionLog, false);
      } else {
        DEBUG_PRINTF("Layout: could not rename %s to %s\n", oldJson.c_str(), newJson.c_str());
      }
    }
  } else {  // timing starts again at this layout's line, in new session files
    beginLapTiming(t);
    sessionStamp = gps.timestampString();  // (the same run: it keeps its session number)
    sessionPath = "";
    startSessionFiles();
  }
  loadBestLaps();  // this layout's
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
// After the first clean lap at a track with no corner sectors: cut that lap
// into sectors (Sectors.h). Laps are timed through them from now on, and
// they're saved with the track when the session ends.
static void makeSectors(const Lap& lap) {
  if (sectorsTried || lap.tags || settingLine) return;
  sectorsTried = true;  // once a session: a track with no braking zones doesn't get them later
  std::vector<sectors::PathPt> path;
  path.reserve(lapTimer.bestPath().size());
  for (const LapTimer::TracePt& p : lapTimer.bestPath()) path.push_back({p.x, p.y, p.t, p.speedKmh});
  std::vector<sectors::Cut> cuts = sectors::findCuts(path);
  if (cuts.empty()) {
    DEBUG_PRINTLN("Sectors: no braking zones on that lap - no corner sectors");
    return;
  }
  const TrackPoint& sf = lapTimer.startFinish();
  std::vector<TrackPoint> gates;
  for (const sectors::Cut& c : cuts) {
    TrackPoint g = {0, 0, (float)DEFAULT_GATE_HALF_WIDTH_M, c.headingDeg, HeadingFrom::Set};
    geo::fromLocal(c.x, c.y, sf.lat, sf.lon, g.lat, g.lon);
    gates.push_back(g);
    DEBUG_PRINTF("Sectors: T%d ends %.1f s into the lap at %.0f km/h\n", (int)gates.size(), c.t / 1000.0,
                 c.speedKmh);
  }
  trackDB.setSectors(selectedIndex, gates);
  lapTimer.setSectors(gates);
}

static void saveMapFromLap() {
  const Track& t = selectedTrack();
  if (t.hasMap || t.bench || lapTimer.bestPath().size() < 10) return;
  std::vector<geo::XYf> line = lapPath(lapTimer.bestPath(), 3.0f);
  if (!t.stage) line.push_back({0, 0});  // close the loop at the start/finish (a stage's run ends elsewhere)
  trackDB.setMap(selectedIndex, line);  // written to tracks.json when the session ends
}

static void onLapEvent(const LapEvent& ev) {
  uint32_t now = millis();
  switch (ev.type) {
    case LapEventType::LapStarted:
      DEBUG_PRINTF("%s %d started\n", lapTimer.isStage() ? "Run" : "Lap", lapTimer.currentLapNumber());
      if (lapTimer.isStage()) {  // no finish yet: the driver taps crossing it
        overlay.notice = lapTimer.hasFinish() ? "" : "Tap as you cross the finish";
        layoutNoticeUntil = now + MAX_LAP_TIME_MS;
      }
      break;

    case LapEventType::RunEnded:
      DEBUG_PRINTF("Run %d ended without a finish - not timed\n", lapTimer.currentLapNumber());
      overlay.notice = lapTimer.hasFinish() ? "Stopped before the finish: no time" : "No finish yet: tap at it next run";
      layoutNoticeUntil = now + 8000;
      break;

    case LapEventType::LapCompleted: {
      const Lap* lap = lapTimer.lastLap();
      DEBUG_PRINTF("LAP %d: %s  (delta %s)%s%s%s  max %.0f km/h, %.0f m\n", lap->number,
                   formatLapTime(lap->timeMs).c_str(), formatDelta(ev.deltaMs).c_str(), ev.isBest ? "  BEST" : "",
                   lap->tags ? "  " : "", laptags::shortLabel(lap->tags), lap->maxSpeedKmh, lap->distanceM);
      overlay.showLap = true;
      overlay.lapNumber = lap->number;
      overlay.lapMs = lap->timeMs;
      overlay.lapIsBest = ev.isBest;
      overlay.lapIsPb = false;
      overlay.lapIsDayBest = false;
      overlay.lapDeltaMs = ev.deltaMs;
      lapOverlayUntil = now + 4000;
      deltaTrend.clear();  // the delta starts again at the line

      // Save after every lap so a power cut doesn't lose the session.
      if (sessionPath.length() == 0) sessionPath = summaryPath(selectedTrack().id);
      if (SessionLog::save(sessionPath, selectedTrack(), sessionStamp, sessionNumber, lapTimer, sessionLog, false)) {
        DEBUG_PRINTF("Saved %s:%s\n", Storage::sessionFSName(), sessionPath.c_str());
      }
      bool switched = false;
      if (!layoutChecked && lapTimer.lapCount() == 1) {  // first full lap: right layout?
        layoutChecked = true;
        switched = checkLayout(lapPath(lapTimer.bestPath(), 5.0f), true);
      }
      if (ev.isBest && !switched) {
        saveMapFromLap();
        overlay.lapIsPb = saveBestLaps(*lap, overlay.lapIsDayBest);
        makeSectors(*lap);
        LedBar::onBestLap(overlay.lapIsPb);
      }
      updateSectorBests();  // with this lap's sectors in
      break;
    }

    case LapEventType::SectorCrossed:  // (used by the debrief later)
      DEBUG_PRINTF("T%d: %s  (delta %s)\n", ev.splitIndex + 1, formatLapTime(ev.timeMs).c_str(),
                   formatDelta(ev.deltaMs).c_str());
      break;

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

// Parked in the pits, off again, or parked so long the session is over (see PitWatch in AutoStart.h).
static void onPitEvent(PitWatch::Event ev) {
  switch (ev) {
    case PitWatch::Event::Parked:
      DEBUG_PRINTF("Parked: in the pits - session %d summary shown, drive off within %lu min to carry on\n",
                   sessionNumber, (unsigned long)(SESSION_RESUME_MS / 60000));
      pitView = true;
      pitPage = 0;
      lastRender = 0;
      if (sessionPath.length() == 0 && lapTimer.lapCount() > 0) sessionPath = summaryPath(selectedTrack().id);
      if (sessionPath.length())
        SessionLog::save(sessionPath, selectedTrack(), sessionStamp, sessionNumber, lapTimer, sessionLog, false);
      buildDebrief();
      saveTrackStats();  // now, in case the power goes before the session ends
      break;
    case PitWatch::Event::MovedOff:
      DEBUG_PRINTF("Driving off - session %d carries on\n", sessionNumber);
      pitView = false;
      lastRender = 0;
      break;
    case PitWatch::Event::SessionOver:
      DEBUG_PRINTF("Parked for %lu min - session %d ended; driving off starts the next\n",
                   (unsigned long)(SESSION_RESUME_MS / 60000), sessionNumber);
      enterState(AppState::Summary);
      nextSessionArmed = true;
      driveOff.reset();
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

// One fix of a lap-timing session
static void onTrackFix(const GpsFix& fix) {
  recordFix(fix, !settingLine && lapTimer.lapRunning() ? lapTimer.currentLapNumber() : 0);
  if (!settingLine) {
    LapEvents evs = lapTimer.onFix(fix.lat, fix.lon, fix.speedKmh, fix.gpsTimeMs, fix.rxMillis, fix.hAccM);
    for (const LapEvent& ev : evs) onLapEvent(ev);
    const int32_t d = lapTimer.liveDeltaMs();
    deltaTrend.add(fix.gpsTimeMs, d, lapTimer.lapRunning() && d != NO_DELTA);
    overlay.trendMs = deltaTrend.change();
    const Track& t = selectedTrack();
    inPitLane = false;
    if (!t.pitLines.empty()) {
      const geo::XY at = geo::toLocal(fix.lat, fix.lon, t.refLat(), t.refLon());
      inPitLane = situation::inPitLane(t.map, t.pitLines, (float)at.x, (float)at.y);
    }
    slowWatch.onFix(fix.speedKmh, fix.gpsTimeMs, inPitLane);
    onPitEvent(pitWatch.onFix(fix.speedKmh, fix.gpsTimeMs));
    // Point to point: stopped at the start, the run is armed
    static bool stageArmed = false;
    if (state == AppState::Running && lapTimer.isStage() && lapTimer.armed() != stageArmed) {
      stageArmed = lapTimer.armed();
      if (stageArmed) {
        DEBUG_PRINTLN("Run armed: the clock starts as the car moves off");
        overlay.notice = "Armed: the clock starts as you go";
        layoutNoticeUntil = millis() + MAX_LAP_TIME_MS;
      } else if (!lapTimer.lapRunning()) {
        overlay.notice = "";  // left the start without going
      }
    }
  }
  if (state == AppState::Running) trackDrivenPath(fix);  // (the session may have just ended)
}

#if TRACK_LEARNING_ENABLED
// Free Roam drove the same loop twice: make it a track with a provisional
// line where the loop began (move it later on the Wi-Fi page and re-time),
// and start a session there that times both loops from the kept fixes. A
// loop round a known track with a line is timed at that track instead.
static void learnTrack(const learn::Loop& lp) {
  double dist;
  int idx = trackDB.findNearest(lp.lat, lp.lon, LEARN_NEAR_TRACK_M, &dist);
  const bool known = idx >= 0 && trackDB.get(idx)->hasStartFinish;
  if (!known) {
    if (idx < 0) idx = trackDB.createTrack(lp.lat, lp.lon);  // else a track here with no line yet gets this one
    if (idx < 0) {
      loopFinder.reset();
      return;
    }
    trackDB.setStartFinish(idx, lp.lat, lp.lon, lp.headingDeg);
  }
  DEBUG_PRINTF("Loop driven twice (%.0f m, %.0f m): %s %s%s, timing both loops\n", lp.lap1M, lp.lap2M,
               known ? "it's" : "new track", trackDB.get(idx)->displayName().c_str(),
               known ? "" : String(", line at " + String(lp.lat, 6) + ", " + String(lp.lon, 6)).c_str());
  replayPos = 0;
  while (replayPos < recentFixes.size() && (int32_t)(recentFixes[replayPos].rxMillis - (lp.t0 - 3000)) < 0) replayPos++;
  selectedIndex = idx;
  enterState(AppState::Running);  // saves the Free Roam drive
  if (!known) layoutChecked = true;  // the loop is the track: don't swap it for one nearby
  replaying = replayPos < recentFixes.size();  // (none kept: timed from now)
  overlay.notice = (known ? "" : "NEW TRACK: ") + selectedTrack().displayName();
  layoutNoticeUntil = millis() + 8000;
}

// The kept fixes, through the lap timer and into the log, a few each loop()
// so the card keeps up. Done when it has caught up with the live ones.
static void replayFixes() {
  for (int n = 0; n < 40 && replayPos < recentFixes.size(); n++) {
    if (LogWriter::bufferedBytes() > LOG_BUFFER_BYTES / 2) return;  // let the card catch up
    const GpsFix f = recentFixes[replayPos++];
    onTrackFix(f);
    if (state != AppState::Running) return;  // (leaving Running stops it)
  }
  if (replayPos < recentFixes.size()) return;
  replaying = false;
  recentFixes.release();
  DEBUG_PRINTF("New track: %d laps timed from the loops driven\n", (int)lapTimer.lapCount());
}
#endif

static void chooseMenuItem(int item) {
  switch (item) {
    case 0:  // Free Roam
      if (gps.hasFix()) enterState(AppState::FreeRoam);
      break;
    case 1:  // New Track: a circuit or a course (the NEW TRACK screen)
      if (gps.hasFix()) {
        newTrackHighlight = 0;
        enterState(AppState::NewTrack);
      }
      break;
    case 2:
      enterState(AppState::TrackSelect);
      break;
    case 3:  // Performance: works without a fix too, for the bench demo
      enterState(AppState::Performance);
      break;
    case 4:
      settingsUi.begin(input.touchAvailable());
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
  LedBar::setBlueOrange(s.deltaColours == DeltaColours::BlueOrange);
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
        enterState(readyFromList ? AppState::TrackSelect : AppState::Menu);
      } else if (ev == InputEvent::TapHeader) {  // next layout of this venue (still waiting for the car to drive off)
        selectedIndex = trackDB.nextLayout(selectedIndex);
        lastRender = 0;
      }
      break;

    case AppState::Running:
      if (ev == InputEvent::Hold || ev == InputEvent::BootLong) enterState(AppState::Summary);
      else if (settingLine) markStartFinish();  // any tap marks the line
      else if (lapTimer.isStage() && lapTimer.lapRunning() && !lapTimer.hasFinish() && !pitView &&
               ev != InputEvent::BootShort)
        markFinish();  // any tap marks the finish
      else if (pitView) {  // parked: a tap or press pages through the debrief, then back to the timing screen
        if (debriefView.r.valid && pitPage == 0) pitPage = 1;
        else pitView = false;
        lastRender = 0;
      }
      else if (slowWatch.slow()) {  // the slow screen has no other view: the screen follows the situation
      } else if (ev == InputEvent::BootShort ||  // a press, or a tap that isn't a bump while moving: switch view
               timing::tapSwitchesView(gps.hasFix(), gps.lastFix().speedKmh, input.lastTapMs())) {
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

    case AppState::Summary:  // MENU button, or on to this track's Ready screen to go out again
      if (tappedWifi(ev, DisplayManager::WIFI_BUTTON_Y) || ev == InputEvent::BootLong) enterState(AppState::Wifi);
      else if (ev == InputEvent::TapLeft && input.lastTapY() >= DisplayManager::WIFI_BUTTON_Y - 4) enterState(AppState::Menu);
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
      const bool tapped = ev == InputEvent::TapLeft || ev == InputEvent::TapCenter || ev == InputEvent::TapRight;
      SettingsScreenState::Result r = tapped ? settingsUi.tap(input.lastTapX(), input.lastTapY(), s)
                                             : settingsUi.handle(toKey(ev), s);
      if (r == SettingsScreenState::Result::Changed) {
        Settings::set(s);
        applySettings();
      } else if (r == SettingsScreenState::Result::Exit) {
        enterState(AppState::Menu);
      }
      lastRender = 0;
      break;
    }

    case AppState::NewTrack:  // Circuit on the left, Course on the right, Back in the middle
      if (ev == InputEvent::TapLeft) createNewTrack(false);
      else if (ev == InputEvent::TapRight) createNewTrack(true);
      else if (ev == InputEvent::TapCenter || ev == InputEvent::TapHeader || ev == InputEvent::Hold) enterState(AppState::Menu);
      else if (ev == InputEvent::BootShort) newTrackHighlight ^= 1;
      else if (ev == InputEvent::BootLong) createNewTrack(newTrackHighlight == 1);
      lastRender = 0;
      break;

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

// The selected track's PB, for the track list and Ready screen
static void showPersonalBest() {
  uint32_t ms, date;
  personalBestFor(selectedIndex, ms, date);
  display.setPersonalBest(ms, date);
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
      showPersonalBest();
      display.drawTrackSelect(selectedTrack(), listPos, (int)listOrder.size(), distanceToTrack(selectedIndex),
                              gps, input.touchAvailable());
      break;
    case AppState::TrackConfirm:
      showPersonalBest();
      display.drawTrackConfirm(selectedTrack(), distanceToTrack(selectedIndex), gps.hasFix(),
                               input.touchAvailable(), trackDB.layoutNumber(selectedIndex),
                               trackDB.layoutCount(selectedIndex), readyFromList, autoStart);
      break;
    case AppState::Running:
      if (settingLine) {
        display.drawSetLine(selectedTrack(), gps, input.touchHeldMs());
        break;
      }
      if (overlay.showLap && now > lapOverlayUntil) overlay.showLap = false;
      if (overlay.splitIndex >= 0 && now > splitOverlayUntil) overlay.splitIndex = -1;
      if (overlay.notice.length() && now > layoutNoticeUntil) overlay.notice = "";
      {
        const int len = settings::sessionMinutes(Settings::get());
        display.setSessionClock(situation::sessionClock(now - sessionStartMs, len).c_str(),
                                situation::sessionEnding(now - sessionStartMs, len));
      }
      if (pitView && debriefView.r.valid)
        display.drawDebrief(selectedTrack(), lapTimer, debriefView, pitPage);
      else if (pitView)  // no laps yet: the log is what's saved
        display.drawSummary(selectedTrack(), lapTimer,
                            sessionPath.length() || sessionLog.length() == 0 ? sessionPath : sessionBase + ".vbo",
                            sessionNumber, true);
      else if (slowWatch.slow() && !overlay.showLap)  // slow, or in the pit lane (the lap flash still shows)
        display.drawSlow(selectedTrack(), lapTimer, gps, inPitLane, input.touchHeldMs());
      else if (showMap) display.drawRunningMap(selectedTrack(), lapTimer, gps, overlay, input.touchHeldMs());
      else display.drawRunning(selectedTrack(), lapTimer, gps, overlay, input.touchHeldMs());
      break;
    case AppState::Summary:
      display.drawSummary(selectedTrack(), lapTimer, sessionPath, sessionNumber, false);
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
      display.drawSettings(Settings::get(), settingsUi, input.touchAvailable(),
                           gps.hasFix() ? (int64_t)settings::localTimeOfDayMs(gps.lastFix().gpsTimeMs,
                                                                               Settings::get().utcOffsetMin)
                                        : -1);
      break;
    case AppState::Setup:
      display.drawSetup(setupUi.step, setupUi.highlight, input.touchAvailable());
      break;
    case AppState::NewTrack:
      display.drawNewTrack(input.touchAvailable() ? -1 : newTrackHighlight, input.touchAvailable());
      break;
    case AppState::Wifi:
      display.drawWifi(WifiPortal::active(), WifiPortal::networkName(), WifiPortal::address(),
                       WifiPortal::clientCount(), WifiPortal::uploadCodeValue(), WifiPortal::updateProgress(),
                       WifiPortal::restarting(), input.touchAvailable());
      break;
  }
}

// Once the GPS has a fix: a track within range is picked. If the car is
// already moving the session starts on it straight away; if it's parked, the
// track's Ready screen waits for the car to drive off (so the menu is one
// button away). With no track in range, show the menu.
static void autoPickTrack() {
  if (autoPickDone || !gps.hasFix()) return;
  if (state != AppState::GpsWait && state != AppState::Menu) return;
  autoPickDone = true;
  double dist;
  int nearest = trackDB.findNearest(gps.lastFix().lat, gps.lastFix().lon, TRACK_DETECTION_RADIUS_M, &dist);
  if (nearest >= 0) {
    selectedIndex = nearest;
    if (gps.lastFix().speedKmh >= AUTO_START_KMH) {
      DEBUG_PRINTF("Nearest track: %s (%.0f m) - already moving, starting a session\n", selectedTrack().name.c_str(), dist);
      enterState(AppState::Running);
    } else {
      DEBUG_PRINTF("Nearest track: %s (%.0f m) - ready, starts when you drive off\n", selectedTrack().name.c_str(), dist);
      enterState(AppState::TrackConfirm);
      autoStart = true;
      driveOff.reset();
    }
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
      if (!replaying) onTrackFix(fix);
      else if (recentFixes.push(fix) && replayPos > 0) replayPos--;  // timed after the kept ones
    } else if (state == AppState::FreeRoam) {
      recordFix(fix, 0);
      freeRoam.onFix(fix.lat, fix.lon, fix.speedKmh);
#if TRACK_LEARNING_ENABLED
      recentFixes.push(fix);
      learn::Loop lp;
      if (loopFinder.onFix(fix.lat, fix.lon, fix.speedKmh, fix.courseDeg, fix.courseValid, fix.rxMillis, &lp))
        learnTrack(lp);
#endif
    } else if (state == AppState::Performance) {
      if (perfRun.onFix(fix.gpsTimeMs, fix.speedKmh, fix.altM) > 0) onPerfResult();
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
    } else if (TRACK_LEARNING_ENABLED && state == AppState::Menu && noTrackNearby) {
      // Away from known tracks, driving off records the drive and watches for a loop
      if (roamDriveOff.onFix(fix.speedKmh, fix.gpsTimeMs)) {
        DEBUG_PRINTLN("Driving off with no track nearby - Free Roam, watching for a loop");
        enterState(AppState::FreeRoam);
      }
    }
  }
#if TRACK_LEARNING_ENABLED
  if (replaying && state == AppState::Running) replayFixes();
#endif

  LogWriter::poll();
  InputEvent ev = input.poll();
  if (ev != InputEvent::None && noteActivity(now)) ev = InputEvent::None;  // the press that wakes the screen does nothing else
  handleInput(ev);

  // A finger dragging the Settings list: the list follows it (and counts as using the board)
  static bool listDragged = false;
  if (state == AppState::SettingsScreen && input.dragging()) {
    noteActivity(now);
    settingsUi.drag(input.dragDy());
    listDragged = true;
  } else if (listDragged) {
    settingsUi.dragEnd();
    listDragged = false;
    lastRender = 0;  // draw the list where it ended
  }

  bool timing = state == AppState::Running && !settingLine && !pitWatch.parked() && lapTimer.lapRunning();
  int32_t liveDelta = lapTimer.liveDeltaMs();
  LedBar::update(timing, gps.hasFix() && liveDelta != NO_DELTA, liveDelta);

  autoPickTrack();
  WifiPortal::loop(gps);
  {  // A re-time finished: the PBs it may have rewritten are read from the card again
    static bool wasRetiming = false;
    const bool retiming = SessionLog::retiming();
    if (wasRetiming && !retiming) pbCache.clear();
    wasRetiming = retiming;
  }
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

  // Dragging the list: draw as fast as the panel allows (about 30 a second) so it follows the finger
  const uint32_t frameMs = listDragged ? 33 : 1000 / DISPLAY_FPS;
  if (!sleepTimer.screenOff() && (lastRender == 0 || now - lastRender >= frameMs)) {
    lastRender = now;
    render();
  }
}
