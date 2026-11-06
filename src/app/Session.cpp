#include "Session.h"

#include <Arduino.h>
#include "App.h"
#include "AutoStart.h"
#include "BestLapStore.h"
#include "CameraBle.h"
#include "Config.h"
#include "CourseRuns.h"
#include "Courses.h"
#include "Debrief.h"
#include "DisplayManager.h"
#include "EngineData.h"
#include "FreeRoam.h"
#include "GPSHandler.h"
#include "GeoMath.h"
#include "LapTimer.h"
#include "LayoutMatch.h"
#include "LogWriter.h"
#include "ProfileStore.h"
#include "Sectors.h"
#include "SessionLog.h"
#include "Settings.h"
#include "SimTrack.h"
#include "SituationScreen.h"
#include "Storage.h"
#include "TimingScreen.h"
#include "TrackDatabase.h"
#include "TrackDay.h"
#include "TrackLearn.h"
#include "TrackRange.h"

namespace Session {

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// The first run's finish tap on a new course can be taken back for FINISH_UNDO_MS. Meanwhile what a finished
// run does (the best lap, the map, the corner sectors) waits, so an undone finish leaves nothing behind.
static uint32_t finishUndoUntil = 0;
static bool finishCommitPending = false;
static void commitFinishLap();

// Lap-timing session
static bool settingLine = false;  // waiting for the driver to tap at the start/finish line
static bool sectorsTried = false; // the track has corner sectors, or this session tried to make them
static int sessionNumber = 0;     // "Session N" of the day at this track
static PitWatch pitWatch;         // parked: in the pits (see AutoStart.h)
static bool pitView = false;      // parked, showing the debrief (or the summary without one); taps page on, then back to timing
static int pitPage = 0;           // the debrief's page
static DebriefView debriefView;   // worked out when the car parks
static String sessionPath;        // its summary (.json)
static std::vector<String> cameraStartsPending;  // until the session has a summary file
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

// ---------------------------------------------------------------------------
// Session files
// ---------------------------------------------------------------------------

// The runs already made on the selected course's day, before this session (CourseRuns.h); 0 for anything else
static int courseRunsBefore() {
  const Track& t = selectedTrack();
  return t.course && CourseRuns::wanted(t.id, t.event) ? (int)CourseRuns::day().count() : 0;
}

static uint32_t minLapMs(const Track& t) {
  if (t.bench) return SIM_MIN_LAP_MS;
  if (t.stage) return STAGE_MIN_RUN_MS;
  return settings::minLapMs(Settings::get(), t.kart);  // the Min lap time setting, or by the kind of track
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

// The summary is saved a moment after a lap, not in the lap event: re-reading and rewriting it takes
// 60-500 ms, which would delay the lap flash and the next fixes. (The lap times themselves come from GPS time.)
static const uint32_t SUMMARY_SAVE_DELAY_MS = 1000;
static bool summaryDirty = false;
static uint32_t summarySaveAt = 0;
static void markSummaryDirty(uint32_t now) {
  if (!summaryDirty) summarySaveAt = now + SUMMARY_SAVE_DELAY_MS;  // (a second after the first change; later ones ride along)
  summaryDirty = true;
}
// Save it now if it is waiting. Before anything that reads the summary or ends the session or the power.
void flushSummary() {
  if (!summaryDirty) return;
  summaryDirty = false;
  if (sessionPath.length() == 0 || state != AppState::Running) return;
  if (SessionLog::save(sessionPath, selectedTrack(), sessionStamp, sessionNumber, lapTimer, sessionLog, false))
    DEBUG_PRINTF("Saved %s:%s\n", Storage::sessionFSName(), sessionPath.c_str());
}

static void saveTrackStats();

static void finishSessionFiles() {
  summaryDirty = false;  // (saved below, finally)
  if (lapTimer.lapCount() > 0 || SessionLog::logging()) {
    if (sessionPath.length() == 0) sessionPath = summaryPath(selectedTrack().id);
    SessionLog::save(sessionPath, selectedTrack(), sessionStamp, sessionNumber, lapTimer, sessionLog, true);
  }
  if (!settingLine) saveTrackStats();
  SessionLog::endLog();
  logPending = false;
}

// ---------------------------------------------------------------------------
// Best laps, and the start of timing
// ---------------------------------------------------------------------------

// Best laps (BestLap.h): the PB and today's best at the track being timed,
// without their paths (the reference lap's path is in the lap timer)
static bestlap::Record pbRecord, dayRecord;
// The track's stats as they were when the session started (best sector
// times ever, the session before): see BestLapStore::Stats
static BestLapStore::Stats trackStats;
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
  TrackDay::forgetPb(selectedIndex);
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

// ---------------------------------------------------------------------------
// The debrief
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Lines, finish and layouts
// ---------------------------------------------------------------------------

// A lap's path with its points at least `spacing` metres apart.
static std::vector<geo::XYf> lapPath(const std::vector<LapTimer::TracePt>& trace, float spacing) {
  std::vector<geo::XYf> out;
  for (const LapTimer::TracePt& p : trace)
    if (out.empty() || hypotf(p.x - out.back().x, p.y - out.back().y) >= spacing) out.push_back({p.x, p.y});
  return out;
}

static void onLapEvent(const LapEvent& ev);
static void saveMapFromLap();
static bool saveBestLaps(const Lap& lap, bool& isDayBest);
static void makeSectors(const Lap& lap);
static void commitFinishLap();

// Is the fix further than TRACK_RANGE_M from the selected track's centre? A line tapped there would be in the
// wrong place for this track (and ignored at the next start), so it is refused with a notice.
static bool tooFarForLine(const GpsFix& f, const char* what) {
  const Track& t = selectedTrack();
  if (trackrange::canStart(t.bench, true, t.centerLat, t.centerLon, f.lat, f.lon) != trackrange::Start::TooFar) return false;
  overlay.notice = "Too far from this track: line not set";
  layoutNoticeUntil = millis() + 6000;
  DEBUG_PRINTF("%s: %.0f km from '%s' - not set\n", what, trackrange::kmFrom(t.centerLat, t.centerLon, f.lat, f.lon),
               t.displayName().c_str());
  return true;
}

// A stage with no finish yet: the driver tapped crossing it. Saves it and ends the run there.
static void markFinish() {
  if (!gps.hasFix()) return;
  const GpsFix& f = gps.lastFix();
  if (tooFarForLine(f, "Finish")) return;
  const bool courseOk = f.courseValid && f.speedKmh >= LINE_HEADING_MIN_KMH;
  LapEvents evs = lapTimer.finishRunNow(f.lat, f.lon, f.gpsTimeMs);
  trackDB.setFinish(selectedIndex, f.lat, f.lon, courseOk ? f.courseDeg : -1);
  lapTimer.setStage(selectedTrack().finish, true);
  overlay.notice = "";
  // The first run of a course can be taken back for a few seconds (VBOX offers the same cancel)
  if (selectedTrack().course && lapTimer.lapCount() == 1 && evs.find(LapEventType::LapCompleted)) finishUndoUntil = millis() + FINISH_UNDO_MS;
  for (const LapEvent& ev : evs) onLapEvent(ev);
  DEBUG_PRINTLN("Finish set - the run ends here");
}

// The finish stands once the Undo window has passed: now the first run can set the best lap, the map
// and the corner sectors, which onLapEvent held back.
static void commitFinishLap() {
  finishUndoUntil = 0;
  if (!finishCommitPending) return;
  finishCommitPending = false;
  const Lap* lap = lapTimer.lastLap();
  if (!lap || lapTimer.bestLapIndex() != (int)lapTimer.lapCount() - 1) return;
  saveMapFromLap();
  overlay.lapIsPb = saveBestLaps(*lap, overlay.lapIsDayBest);
  makeSectors(*lap);
}

// Undo: the finish is gone again, and the first run is still being timed.
static void undoFinish() {
  finishUndoUntil = 0;
  finishCommitPending = false;
  if (!lapTimer.undoFinishRun()) return;
  trackDB.clearFinish(selectedIndex);
  trackDB.discardUnsavedMap(selectedIndex);
  lapTimer.setStage(selectedTrack().finish, false);
  overlay.showLap = false;
  lapOverlayUntil = 0;
  overlay.notice = "Finish taken back: tap at it again";
  layoutNoticeUntil = millis() + 8000;
  if (sessionPath.length()) markSummaryDirty(millis());  // the summary was saved with that run: saved again in a moment
  DEBUG_PRINTLN("Finish taken back - the run goes on");
}

// The driver tapped while crossing the start/finish line: save it and start lap 1.
static void markStartFinish() {
  if (!gps.hasFix()) {
    DEBUG_PRINTLN("Start/finish: no GPS fix - not set");
    return;
  }
  const GpsFix& f = gps.lastFix();
  if (tooFarForLine(f, "Start/finish")) return;
  // The direction of travel is saved with the line, so crossing it the other way doesn't count
  bool courseOk = f.courseValid && f.speedKmh >= LINE_HEADING_MIN_KMH;
  trackDB.setStartFinish(selectedIndex, f.lat, f.lon, courseOk ? f.courseDeg : -1);
  settingLine = false;
  beginLapTiming(selectedTrack());
  TrackDay::forgetPb(selectedIndex);  // the PB may not count at the new line
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
        markSummaryDirty(millis());  // under its new name, in a moment
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
    TrackPoint g = {0, 0, settings::defaultGateM(selectedTrack().kart), c.headingDeg, HeadingFrom::Set};
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

// ---------------------------------------------------------------------------
// Lap and pit events
// ---------------------------------------------------------------------------

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
      overlay.isRun = lapTimer.isStage();
      // A run is numbered across the day's sessions (CourseRuns.h); a lap by the session
      overlay.lapNumber = overlay.isRun ? courseRunsBefore() + (int)lapTimer.lapCount() : lap->number;
      overlay.lapMs = lap->timeMs;
      overlay.lapIsBest = ev.isBest;
      overlay.lapIsPb = false;
      overlay.lapIsDayBest = false;
      overlay.lapDeltaMs = ev.deltaMs;
      lapOverlayUntil = now + 4000;
      deltaTrend.clear();  // the delta starts again at the line

      // Saved after every lap so a power cut doesn't lose the session: a second later, from loop()
      if (sessionPath.length() == 0) sessionPath = summaryPath(selectedTrack().id);
      markSummaryDirty(now);
      bool switched = false;
      if (!layoutChecked && lapTimer.lapCount() == 1) {  // first full lap: right layout?
        layoutChecked = true;
        switched = checkLayout(lapPath(lapTimer.bestPath(), 5.0f), true);
      }
      if (ev.isBest && !switched) {
        if (finishUndoUntil) {  // the finish was just tapped: it can still be taken back
          finishCommitPending = true;
        } else {
          saveMapFromLap();
          overlay.lapIsPb = saveBestLaps(*lap, overlay.lapIsDayBest);
          makeSectors(*lap);
        }
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
      summaryDirty = false;
      if (sessionPath.length())  // (now: the summary is on screen, and the car is parked)
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
      if (lapTimer.isStage()) break;  // waiting in line between runs: the day's runs stay one session
      DEBUG_PRINTF("Parked for %lu min - session %d ended; driving off starts the next\n",
                   (unsigned long)(SESSION_RESUME_MS / 60000), sessionNumber);
      enterState(AppState::Summary);
      armNextSession();
      break;
    default:
      break;
  }
}

// One fix of a lap-timing session
static void onTrackFix(const GpsFix& fix) {
  recordFix(fix, !settingLine && lapTimer.lapRunning() ? lapTimer.currentLapNumber() : 0);
  if (!settingLine) {
    float coolC = 0, oilC = 0;  // the laps' hottest (6.5)
    const bool coolKnown = EngineData::coolant(millis(), coolC), oilKnown = EngineData::oil(millis(), oilC);
    lapTimer.noteTemps(coolKnown, coolC, oilKnown, oilC);
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

// ---------------------------------------------------------------------------
// Free Roam and learned tracks
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// SYNC and the camera
// ---------------------------------------------------------------------------

// The WI-FI button, in the right third of the menu and summary screens
static bool tappedWifi(InputEvent ev, int buttonTop) {
  return ev == InputEvent::TapRight && input.lastTapY() >= buttonTop - 4;
}

// SYNC: noted with the session (its summary) at the moment it appears
static void startSyncFlash() {
  if (!haveCameraFix) return;  // no GPS time to show
  syncUntil = millis() + SYNC_FLASH_MS;
  if (!syncUntil) syncUntil = 1;
  const String utc = gpsTimeAt(cameraFix, millis());
  flushSummary();  // (the note is added to the file)
  const bool saved = sessionPath.length() && SessionLog::addSyncFlash(sessionPath, utc);
  DEBUG_PRINTF("SYNC flash at %s%s\n", utc.c_str(), saved ? "" : " (no summary to note it in)");
  lastRender = 0;
}

// The camera started recording: note the GPS time with the session
void noteCameraStart() {
  uint32_t at;
  if (CameraBle::takeStarted(at) && haveCameraFix && state == AppState::Running) {
    cameraStartsPending.push_back(gpsTimeAt(cameraFix, at));
    DEBUG_PRINTF("Camera: recording from %s\n", cameraStartsPending.back().c_str());
  }
  if (!cameraStartsPending.empty() && sessionPath.length()) {
    flushSummary();  // (the notes are added to the file)
    for (const String& s : cameraStartsPending) SessionLog::addCameraStart(sessionPath, s);
    cameraStartsPending.clear();
  }
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

// enterState(): a lap-timing session starts or ends (state is still the screen being left)
void stateChanging(AppState next) {
  if (next == AppState::Running) {
    const Track& t = selectedTrack();
    gps.setSimulated(t.bench);
    trackDB.loadMap(selectedIndex);
    if (!t.hasStartFinish) settingLine = true;
    beginLapTiming(t);
    sessionStamp = gps.timestampString();
    sessionNumber = Settings::nextSessionNumber(t.id.c_str(), sessionStamp.c_str());
    sessionPath = "";
    cameraStartsPending.clear();
    startSessionFiles();
    loadBestLaps();
    pitWatch.reset();
    pitView = false;
    slowWatch.reset();
    inPitLane = false;
    sessionStartMs = millis();
    layoutChecked = t.bench || t.stage;  // a stage is its own course
    finishUndoUntil = 0;
    finishCommitPending = false;
    if (t.course && t.event.length()) CourseRuns::ensure(t.id, t.event);  // the day's runs so far, read in the background
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
    commitFinishLap();  // a finish tap still open for Undo stands
    if (selectedTrack().course) CourseRuns::markStale();  // this session's runs are in its summary now
    finishSessionFiles();
    trackDB.keepGuessedHeadings(selectedIndex, lapTimer.startFinish(), lapTimer.splits());
    trackDB.saveMaps();  // the map from this session's best lap: saved now that nothing is being timed
    gps.setSimulated(false);
    settingLine = false;
    pitView = false;
  }
}

// enterState(): Free Roam starts or ends
void freeRoamChanging(AppState next) {
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
}

// A tap or press while timing
void onInputRunning(InputEvent ev) {
  if (ev == InputEvent::Hold || ev == InputEvent::BootLong) enterState(AppState::Summary);
  else if (finishUndoUntil && ev != InputEvent::TapHeader) undoFinish();  // any tap or press in the Undo window
  else if (settingLine) markStartFinish();  // any tap marks the line
  else if (lapTimer.isStage() && lapTimer.lapRunning() && !lapTimer.hasFinish() && !pitView &&
           ev != InputEvent::BootShort)
    markFinish();  // any tap marks the finish
  else if (pitView && ev == InputEvent::TapHeader) startSyncFlash();  // for lining up a video (6.4)
  else if (pitView) {  // parked: a tap or press pages through the debrief, then back to the timing screen
    if (debriefView.r.valid && pitPage == 0) pitPage = 1;
    else pitView = false;
    lastRender = 0;
  }
  else if (slowWatch.slow() && !(lapTimer.isStage() && lapTimer.lapRunning())) {  // the slow screen has no other view
  } else {  // a tap or a BOOT press switches between the timing and map views
    showMap = !showMap;
    lastRender = 0;
  }
}

// The summary: MENU button, or on to this track's Ready screen to go out again
void onInputSummary(InputEvent ev) {
  if (ev == InputEvent::TapHeader) startSyncFlash();  // the top bar: SYNC, for lining up a video (6.4)
  else if (tappedWifi(ev, DisplayManager::WIFI_BUTTON_Y) || ev == InputEvent::BootLong) enterState(AppState::Wifi);
  else if (ev == InputEvent::TapLeft && input.lastTapY() >= DisplayManager::WIFI_BUTTON_Y - 4) enterState(AppState::Menu);
  else if (ev != InputEvent::Hold) enterState(AppState::TrackConfirm);
}

void onInputFreeRoam(InputEvent ev) {
  if (ev == InputEvent::Hold || ev == InputEvent::BootLong) enterState(AppState::FreeRoamSummary);
}

void onInputFreeRoamSummary(InputEvent ev) {
  if (tappedWifi(ev, DisplayManager::WIFI_BUTTON_Y) || ev == InputEvent::BootLong) enterState(AppState::Wifi);
  else if (ev != InputEvent::Hold) enterState(AppState::Menu);
}

// Runs on a course count on through the day (CourseRuns.h); the finish tap has a few seconds of Undo
void beforeRender(uint32_t now) {
  display.setRunBase(state == AppState::Running || state == AppState::Summary ? courseRunsBefore() : 0);
  display.setFinishUndo(state == AppState::Running && finishUndoUntil && (int32_t)(finishUndoUntil - now) > 0
                            ? finishUndoUntil - now
                            : 0,
                        input.touchAvailable());
}

void renderRunning(uint32_t now) {
  if (settingLine) {
    display.drawSetLine(selectedTrack(), gps, input.touchHeldMs());
    return;
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
  else if (slowWatch.slow() && !overlay.showLap && !(lapTimer.isStage() && lapTimer.lapRunning()))  // slow, or in the pit lane (the lap flash still shows; never mid-run on a stage)
    display.drawSlow(selectedTrack(), lapTimer, gps, inPitLane, input.touchHeldMs());
  else if (showMap) display.drawRunningMap(selectedTrack(), lapTimer, gps, overlay, input.touchHeldMs());
  else display.drawRunning(selectedTrack(), lapTimer, gps, overlay, input.touchHeldMs());
}

void renderSummary() {
  display.drawSummary(selectedTrack(), lapTimer, sessionPath, sessionNumber, false);
}

void renderFreeRoam() {
  display.drawFreeRoam(freeRoam, gps, input.touchHeldMs());
}

void renderFreeRoamSummary() {
  display.drawFreeRoamSummary(freeRoam, freeRoamDurationMs, freeRoamPath);
}

// loop(): a fix while timing
void onFixRunning(const GpsFix& fix) {
  if (!replaying) onTrackFix(fix);
  else if (recentFixes.push(fix) && replayPos > 0) replayPos--;  // timed after the kept ones
}

// loop(): a fix in Free Roam
void onFixFreeRoam(const GpsFix& fix) {
  recordFix(fix, 0);
  freeRoam.onFix(fix.lat, fix.lon, fix.speedKmh);
#if TRACK_LEARNING_ENABLED
  recentFixes.push(fix);
  learn::Loop lp;
  if (loopFinder.onFix(fix.lat, fix.lon, fix.speedKmh, fix.courseDeg, fix.courseValid, fix.rxMillis, &lp))
    learnTrack(lp);
#endif
}

// Parked in the pits (the camera records only while the session is moving)
bool parked() {
  return pitWatch.parked();
}

// loop(): the kept fixes of a learned track, a few at a time
void replayTick() {
#if TRACK_LEARNING_ENABLED
  if (replaying && state == AppState::Running) replayFixes();
#endif
}

// loop(): once a pass
void poll(uint32_t now) {
  if (state == AppState::Running) {
    CourseRuns::setSkip(sessionPath);  // this session's own summary is followed from the lap timer
    if (finishUndoUntil && (int32_t)(now - finishUndoUntil) >= 0) commitFinishLap();  // the Undo window is over
  }
}

// loop(): the summary is saved a second after the last change
void flushIfDue(uint32_t now) {
  if (summaryDirty && (int32_t)(now - summarySaveAt) >= 0) flushSummary();  // the last lap's, now it is over
}

// The session's track was picked by the driver (+ New circuit, a new course), so the layout check doesn't swap it
// for one nearby
void keepLayout() { layoutChecked = true; }

// A track's Ready screen: the driver chose to mark a new start/finish line
void markNewLine() {
  settingLine = true;
  layoutChecked = true;
}

// A note on the timing screen for showMs
void notice(const String& text, uint32_t showMs) {
  overlay.notice = text;
  layoutNoticeUntil = millis() + showMs;
}

}  // namespace Session
