/**
 * Track Rat - Session log implementation
 */

#include "SessionLog.h"
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <atomic>
#include <memory>
#include <mutex>
#include "Config.h"
#include "EngineData.h"
#include "Imu.h"
#include "JsonFile.h"
#include "LogWriter.h"
#include "BestLapStore.h"
#include "Retime.h"
#include "SessionReplay.h"
#include "Storage.h"
#include "VboFormat.h"

String formatLapTime(uint32_t ms, int decimals) {
  uint32_t minutes = ms / 60000;
  uint32_t seconds = (ms / 1000) % 60;
  uint32_t frac = ms % 1000;
  char buf[20];
  if (decimals <= 0) {
    snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
  } else if (decimals == 1) {
    snprintf(buf, sizeof(buf), "%lu:%02lu.%01lu", (unsigned long)minutes, (unsigned long)seconds,
             (unsigned long)(frac / 100));
  } else {
    snprintf(buf, sizeof(buf), "%lu:%02lu.%03lu", (unsigned long)minutes, (unsigned long)seconds,
             (unsigned long)frac);
  }
  return String(buf);
}

String formatDelta(int32_t ms) {
  if (ms == NO_DELTA) return String("--");
  char buf[16];
  uint32_t a = ms < 0 ? -ms : ms;
  snprintf(buf, sizeof(buf), "%c%lu.%03lu", ms < 0 ? '-' : '+', (unsigned long)(a / 1000),
           (unsigned long)(a % 1000));
  return String(buf);
}

namespace SessionLog {

String fileName(const String& path) { return path.substring(path.lastIndexOf('/') + 1); }

// Base names handed out since boot: their files belong to this boot's
// sessions, so the background recovery leaves them alone.
static std::mutex namesLock;
static std::vector<String> namesThisBoot;

static bool newThisBoot(const String& base) {
  std::lock_guard<std::mutex> lock(namesLock);
  for (const String& n : namesThisBoot) {
    if (n == base) return true;
  }
  return false;
}

static bool baseTaken(fs::FS& fs, const String& base) {
  return fs.exists(base + ".json") || fs.exists(base + ".vbo") || fs.exists(base + ".json.tmp") ||
         newThisBoot(base);
}

String newSessionBase(const String& stamp, const String& name) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs) return String();
  if (!fs->exists(SESSION_DIR)) fs->mkdir(SESSION_DIR);

  String first = String(SESSION_DIR) + "/" + (stamp.length() ? stamp : String("session")) + "_" + name;
  String base = first;
  for (int n = 2; baseTaken(*fs, base) && n < 1000; n++) base = first + "_" + n;
  std::lock_guard<std::mutex> lock(namesLock);
  namesThisBoot.push_back(base);  // before any of its files exist
  return base;
}

bool save(const String& path, const Track& track, const String& stamp, int session, const LapTimer& timer,
          const String& logName, bool finished, bool recovered) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || path.length() == 0) return false;

  size_t splitTimes = 0;
  for (size_t i = 0; i < timer.lapCount(); i++) splitTimes += timer.lap(i).splitMs.size() + timer.lap(i).sectorMs.size();
  DynamicJsonDocument doc(4096 + timer.lapCount() * 384 + splitTimes * 16);  // a lap with every tag takes about 300
  doc["track"] = track.id;
  doc["trackName"] = track.name;
  if (track.layoutName.length()) doc["layout"] = track.layoutName;
  doc["started"] = stamp;
  if (session > 0) doc["session"] = session;
  if (logName.length()) doc["log"] = logName;
  doc["finished"] = finished;
  if (recovered) doc["recovered"] = true;
  doc["lapCount"] = timer.lapCount();
  doc["cleanLaps"] = timer.untaggedLapCount();
  if (uint32_t avg = timer.averageLapMs()) doc["averageMs"] = avg;  // of the untagged laps
  if (const Lap* best = timer.bestLap()) {
    doc["bestLap"] = best->number;
    doc["bestTimeMs"] = best->timeMs;
    doc["bestTime"] = formatLapTime(best->timeMs);
  }
  doc["topSpeedKmh"] = serialized(String(timer.sessionMaxSpeedKmh(), 1));

  JsonArray laps = doc.createNestedArray("laps");
  for (size_t i = 0; i < timer.lapCount(); i++) {
    const Lap& l = timer.lap(i);
    JsonObject o = laps.createNestedObject();
    o["lap"] = l.number;
    o["timeMs"] = l.timeMs;
    o["time"] = formatLapTime(l.timeMs);
    o["maxKmh"] = serialized(String(l.maxSpeedKmh, 1));
    o["avgKmh"] = serialized(String(l.avgSpeedKmh, 1));
    o["distanceM"] = (int32_t)lroundf(l.distanceM);
    if (l.tags) {  // "pit", "slow", "suspect"; why it's suspect: "gps gap", "accuracy", "distance"
      JsonArray tags = o.createNestedArray("tags");
      for (const char* t : laptags::names(l.tags)) tags.add(t);
      if (laptags::suspect(l.tags)) {
        JsonArray why = o.createNestedArray("suspect");
        for (const char* r : laptags::suspectReasons(l.tags)) why.add(r);
      }
    }
    if (!l.splitMs.empty()) {
      JsonArray s = o.createNestedArray("splitsMs");
      for (int32_t v : l.splitMs) s.add(v);
    }
    if (!l.sectorMs.empty()) {  // corner sectors: elapsed at the end of T1, T2... (the last ends at the line)
      JsonArray s = o.createNestedArray("sectorsMs");
      for (int32_t v : l.sectorMs) s.add(v);
    }
  }
  return writeJsonFile(*fs, path, doc, true) > 0;
}

bool saveFreeRoam(const String& path, const String& stamp, const FreeRoam& drive, uint32_t durationMs,
                  const String& logName, bool recovered) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || path.length() == 0 || (!drive.hasPosition() && logName.length() == 0)) return false;

  const bool withRoute = logName.length() == 0;  // the full log has the route at every fix
  const size_t step = max((size_t)1, (size_t)(10.0f / FreeRoam::MIN_POINT_SPACING_M));
  const size_t points = withRoute ? drive.path.size() / step + 2 : 0;
  DynamicJsonDocument doc(4096 + points * 80);  // a [lat, lon] pair takes up to 71 bytes
  doc["type"] = "freeroam";
  doc["started"] = stamp;
  if (logName.length()) doc["log"] = logName;
  doc["finished"] = true;
  if (recovered) doc["recovered"] = true;
  doc["durationMs"] = durationMs;
  doc["duration"] = formatLapTime(durationMs, 0);
  doc["distanceKm"] = serialized(String(drive.distanceKm(), 3));
  doc["topSpeedKmh"] = serialized(String(drive.topSpeedKmh(), 1));
  doc["avgMovingKmh"] = serialized(String(drive.avgMovingKmh(), 1));
  if (withRoute) {
    JsonArray route = doc.createNestedArray("route");
    for (size_t i = 0; i < drive.path.size(); i += step) {
      double lat, lon;
      geo::fromLocal(drive.path[i].x, drive.path[i].y, drive.refLat, drive.refLon, lat, lon);
      JsonArray p = route.createNestedArray();
      p.add(serialized(String(lat, 6)));
      p.add(serialized(String(lon, 6)));
    }
  }
  if (!writeJsonFile(*fs, path, doc, false)) return false;
  DEBUG_PRINTF("Session: free roam saved to %s:%s\n", Storage::sessionFSName(), path.c_str());
  return true;
}

// --- Full-rate log ----------------------------------------------------------

static bool logOpen = false;
static String logPath;
static uint32_t fixCount = 0;
static uint32_t fixesMissed = 0;
static vbo::GForce gforce;
static bool logEngine = false;  // the log has the engine channels (OBD-II over CAN or Bluetooth)
static uint32_t lastFixMs = 0;  // GPS time of the last fix logged (for the gap mark)

void begin() {
  if (Storage::sdMounted()) LogWriter::begin(SD_MMC);
  else DEBUG_PRINTLN("Session: no SD card - sessions keep lap times only; logging every fix (.vbo) needs a card");
}

bool fullLogAvailable() { return Storage::sdMounted() && LogWriter::running(); }
bool logging() { return logOpen; }
uint32_t fixesLogged() { return fixCount; }

static vbo::Header headerFor(const String& stamp, const GpsFix& first) {
  vbo::Header h;
  h.year = first.year;
  h.month = first.month;
  h.day = first.day;
  h.utcMs = first.gpsTimeMs;
  h.comments.push_back("Track Rat GPS lap timer");
  h.comments.push_back(std::string("Started: ") + stamp.c_str());
  return h;
}

static bool beginLog(const String& path, const vbo::Header& h) {
  if (logOpen) endLog();
  if (!fullLogAvailable() || path.length() == 0) return false;
  vbo::Header withEngine = h;
  withEngine.engine = EngineData::logged();  // OBD-II over CAN or Bluetooth: four more channels
  std::string text = vbo::headerText(withEngine);
  if (!LogWriter::open(path, text.data(), text.size())) return false;
  logEngine = withEngine.engine;
  logOpen = true;
  logPath = path;
  fixCount = fixesMissed = 0;
  gforce.reset();
  return true;
}

bool beginTrackLog(const String& vboPath, const Track& t, const String& stamp, int session, bool timing,
                   uint32_t minLapMs, const GpsFix& first) {
  vbo::Header h = headerFor(stamp, first);
  char buf[96];
  h.comments.push_back(t.stage ? "Type: stage" : "Type: laps");
  h.comments.push_back(std::string("Track: ") + t.id.c_str());
  h.comments.push_back(std::string("Track name: ") + t.name.c_str());
  if (session > 0) h.comments.push_back("Session: " + std::to_string(session));
  if (timing) {
    // Exactly what the lap timer uses, direction included, so the session can be timed again from the log
    h.comments.push_back("Start/finish: " + vbo::pointText(t.startFinish));
    for (size_t i = 0; i < t.splits.size(); i++)
      h.comments.push_back("Split " + std::to_string(i + 1) + ": " + vbo::pointText(t.splits[i]));
    for (size_t i = 0; i < t.sectors.size(); i++)
      h.comments.push_back("Sector " + std::to_string(i + 1) + ": " + vbo::pointText(t.sectors[i]));
    if (t.stage && t.hasFinish) h.comments.push_back("Finish: " + vbo::pointText(t.finish));
    // Circuit Tools needs a line across the track, so it needs the track's
    // direction at the start/finish: from the stored map, if there is one.
    if (vbo::gateFromMap(t.map, t.refLat(), t.refLon(), t.startFinish, h.start)) {
      h.hasStart = true;
      for (const TrackPoint& sp : t.splits) {
        vbo::Gate g;
        if (!vbo::gateFromMap(t.map, t.refLat(), t.refLon(), sp, g)) {
          h.splits.clear();  // all or none, so Circuit Tools' split numbers match ours
          break;
        }
        h.splits.push_back(g);
      }
      for (const TrackPoint& sp : t.sectors) {  // made from a lap, so they know their direction
        vbo::Gate g;
        if (!vbo::gateAcross(sp, g)) {
          h.sectors.clear();  // all or none, as the splits
          break;
        }
        h.sectors.push_back(g);
      }
    }
  }
  // A stage's finish for Circuit Tools: across the road the way it was crossed, or from the map
  if (h.hasStart && t.stage && t.hasFinish)
    h.hasFinish = vbo::gateAcross(t.finish, h.finish) || vbo::gateFromMap(t.map, t.refLat(), t.refLon(), t.finish, h.finish);
  snprintf(buf, sizeof(buf), "Min lap ms: %lu", (unsigned long)minLapMs);
  h.comments.push_back(buf);

  if (!beginLog(vboPath, h)) return false;
  DEBUG_PRINTF("Log: recording every fix to %s (%s)\n", vboPath.c_str(),
               h.hasStart ? (h.splits.empty() && !t.splits.empty() ? "with the start/finish for Circuit Tools, no splits"
                                                                    : "with timing lines for Circuit Tools")
                          : "no timing line: set it in Circuit Tools");
  return true;
}

bool beginFreeRoamLog(const String& vboPath, const String& stamp, const GpsFix& first) {
  vbo::Header h = headerFor(stamp, first);
  h.comments.push_back("Type: freeroam");
  if (!beginLog(vboPath, h)) return false;
  DEBUG_PRINTF("Log: recording every fix to %s\n", vboPath.c_str());
  return true;
}

void logFix(const GpsFix& f, uint16_t lap) {
  if (!logOpen) return;
  gforce.update(f.gpsTimeMs, f.speedKmh, f.courseDeg, f.courseValid);
  vbo::Sample s;
  s.utcMs = f.gpsTimeMs;
  s.lat = f.lat;
  s.lon = f.lon;
  s.speedKmh = f.speedKmh;
  s.headingDeg = f.courseDeg;
  s.heightM = f.altM;
  s.sats = f.sats;
  s.hdop = f.pdop;  // HDOP with an NMEA GPS, PDOP with UBX
  s.lap = lap;
  s.latG = gforce.lateral();
  s.longG = gforce.longitudinal();
  imu::CarG g;
  imu::CarRates rates;
  if (Imu::carG(g, rates)) {  // an IMU that knows how it's mounted: real g
    s.latG = g.lat;
    s.longG = g.lon;
  }
  const int32_t gap = fixCount + fixesMissed ? vbo::utcDiffMs(lastFixMs, f.gpsTimeMs) : 0;
  s.tag = laptags::fixFlags(f.speedKmh, f.hAccM, gap > 0 ? (uint32_t)gap : 0);
  lastFixMs = f.gpsTimeMs;
  if (logEngine) {  // 0 for anything the engine hasn't said in the last second
    const obd::Values& e = EngineData::values();
    const uint32_t now = millis();
    s.rpm = e.rpmKnown(now) ? e.rpm : 0;
    s.throttlePct = e.throttleKnown(now) ? e.throttlePct : 0;
    s.coolantC = e.coolantKnown(now) ? e.coolantC : 0;
    s.intakeC = e.intakeKnown(now) ? e.intakeC : 0;
  }
  char line[160];
  size_t n = vbo::formatLine(s, line, sizeof(line), logEngine);
  if (n && LogWriter::append(line, n)) fixCount++;
  else fixesMissed++;
}

void endLog() {
  if (!logOpen) return;
  LogWriter::close();
  logOpen = false;
  DEBUG_PRINTF("Log: %lu fixes logged to %s%s\n", (unsigned long)fixCount, logPath.c_str(),
               fixesMissed ? " (some NOT logged: buffer full)" : "");
}

// --- Recovery at boot ---------------------------------------------------------
//
// Runs in its own task on core 0 at idle priority, so a long log (about 6 s
// per hour of driving) doesn't hold up boot, and timing on core 1 isn't
// touched. It only reads the copy of the tracks taken at boot, never trackDB,
// which the main loop changes.

static std::vector<Track> recoveryTracks;  // lines and names only, no maps
static std::atomic<bool> recoveryRunning{false};

static const Track* recoveryTrack(const std::string& id) {
  for (const Track& t : recoveryTracks) {
    if (t.id == id.c_str()) return &t;
  }
  return nullptr;
}

static bool summaryFinished(fs::FS& fs, const String& jsonPath) {
  if (!fs.exists(jsonPath)) return false;
  File f = fs.open(jsonPath, "r");
  if (!f) return false;
  StaticJsonDocument<32> filter;
  filter["finished"] = true;
  StaticJsonDocument<64> doc;
  DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filter));
  f.close();
  return !err && (doc["finished"] | false);
}

// The track the summary names: empty if there's no summary or it can't be read.
static std::string summaryTrack(fs::FS& fs, const String& jsonPath) {
  if (!fs.exists(jsonPath)) return std::string();
  File f = fs.open(jsonPath, "r");
  if (!f) return std::string();
  StaticJsonDocument<32> filter;
  filter["track"] = true;
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filter));
  f.close();
  return err ? std::string() : std::string(doc["track"] | "");
}

static bool validJson(fs::FS& fs, const String& path) {
  File f = fs.open(path, "r");
  if (!f) return false;
  StaticJsonDocument<16> filter;  // checks the syntax of the whole file, keeps nothing
  filter["-"] = true;
  StaticJsonDocument<64> doc;
  DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filter));
  f.close();
  return !err;
}

// Replays a log through the lap timer (or Free Roam) and writes its summary.
static void recoverSession(fs::FS& fs, const String& vboPath) {
  File f = fs.open(vboPath, "r");
  if (!f) return;
  uint32_t startMs = millis();

  // A log with no start/finish in its header had the line marked during the
  // session: it's in tracks.json now.
  std::unique_ptr<SessionReplay> replay(
      new SessionReplay([](const std::string& id, TrackPoint& sf, std::vector<TrackPoint>& splits) {
        const Track* t = recoveryTrack(id);
        if (!t || !t->hasStartFinish) return false;
        sf = t->startFinish;
        splits = t->splits;
        return true;
      }));
  replay->finishLookup = [](const std::string& id, TrackPoint& finish) {
    const Track* t = recoveryTrack(id);
    if (!t || !t->stage || !t->hasFinish) return false;
    finish = t->finish;
    return true;
  };
  // A session switched to another layout mid-session kept its laps, and its
  // summary names that layout, but the log's header still names the first.
  String base = vboPath.substring(0, vboPath.length() - 4);
  replay->summaryTrackId = summaryTrack(fs, base + ".json");
  static char chunk[4096];
  for (;;) {
    int n = f.read((uint8_t*)chunk, sizeof(chunk));
    if (n <= 0) break;
    replay->feed(chunk, n);
  }
  f.close();
  replay->finish();

  String stamp = replay->stamp.c_str();
  String logName = fileName(vboPath);
  bool ok;
  if (replay->freeRoam) {
    ok = saveFreeRoam(base + ".json", stamp, replay->drive, replay->durationMs, logName, true);
  } else {
    Track track;
    if (const Track* known = recoveryTrack(replay->trackId)) track = *known;
    track.id = replay->trackId.c_str();
    if (replay->trackName.size()) track.name = replay->trackName.c_str();
    ok = save(base + ".json", track, stamp, replay->session, replay->timer, logName, true, true);
  }
  const Lap* best = replay->timer.bestLap();
  DEBUG_PRINTF("Session: %s %s from its log: %lu fixes, %u laps%s%s%s%s, %lu bad lines%s (%lu ms)\n",
               ok ? "recovered" : "could NOT recover", logName.c_str(), (unsigned long)replay->samples,
               (unsigned)replay->timer.lapCount(), best ? ", best " : "",
               best ? formatLapTime(best->timeMs).c_str() : "", replay->switchedLayout ? ", timed as " : "",
               replay->switchedLayout ? replay->trackId.c_str() : "", (unsigned long)replay->reader().badLines(),
               replay->cutLine ? ", last line cut short" : "", (unsigned long)(millis() - startMs));
}

// ("/sessions/a.json.tmp", ".json.tmp") -> "/sessions/a"
static String withoutExt(const String& path, const char* ext) {
  return path.substring(0, path.length() - strlen(ext));
}

static void recoverAll(fs::FS& fs) {
  std::vector<String> logs, temps;
  File dir = fs.open(SESSION_DIR);
  if (!dir || !dir.isDirectory()) return;
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    String name = fileName(e.name());
    if (!e.isDirectory()) {
      if (name.endsWith(".vbo")) logs.push_back(String(SESSION_DIR) + "/" + name);
      else if (name.endsWith(".json.tmp")) temps.push_back(String(SESSION_DIR) + "/" + name);
    }
    e.close();
  }
  dir.close();

  // A summary caught mid-save: keep the new copy if it's complete. One of
  // this boot's sessions is a save in progress: not ours to touch.
  for (const String& tmp : temps) {
    if (newThisBoot(withoutExt(tmp, ".json.tmp"))) continue;
    String json = tmp.substring(0, tmp.length() - 4);
    if (!fs.exists(json) && validJson(fs, tmp) && fs.rename(tmp, json)) {
      DEBUG_PRINTF("Session: finished saving %s\n", json.c_str());
    } else {
      fs.remove(tmp);
    }
  }

  int recovered = 0;
  for (const String& log : logs) {
    String base = withoutExt(log, ".vbo");
    if (newThisBoot(base) || summaryFinished(fs, base + ".json")) continue;
    recoverSession(fs, log);
    recovered++;
  }
  if (recovered) DEBUG_PRINTF("Session: %d unfinished session(s) recovered\n", recovered);
}

static void recoveryTaskMain(void* arg) {
  recoverAll(*(fs::FS*)arg);
  std::vector<Track>().swap(recoveryTracks);
  recoveryRunning = false;
  vTaskDelete(nullptr);
}

void recoverUnfinished(const TrackDatabase& tracks) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || !fs->exists(SESSION_DIR) || recoveryRunning) return;

  recoveryTracks.clear();
  for (int i = 0; i < tracks.count(); i++) {
    const Track* t = tracks.get(i);
    Track c;  // without the map
    c.id = t->id;
    c.name = t->name;
    c.venueId = t->venueId;
    c.layoutName = t->layoutName;
    c.hasStartFinish = t->hasStartFinish;
    c.startFinish = t->startFinish;
    c.splits = t->splits;
    c.sectors = t->sectors;
    c.stage = t->stage;
    c.hasFinish = t->hasFinish;
    c.finish = t->finish;
    recoveryTracks.push_back(c);
  }
  // Idle priority on core 0: the log writer, Wi-Fi and BLE go first, and the
  // idle task still gets its turn, so the watchdog stays fed.
  recoveryRunning = true;
  if (xTaskCreatePinnedToCore(recoveryTaskMain, "recovery", 8192, fs, tskIDLE_PRIORITY, nullptr, 0) != pdPASS) {
    DEBUG_PRINTLN("Session: could not start the recovery task - recovering now");
    recoveryRunning = false;
    recoverAll(*fs);
    std::vector<Track>().swap(recoveryTracks);
  }
}

bool recovering() { return recoveryRunning; }

// ---------------------------------------------------------------------------
// Re-timing: like recovery, a task on core 0 at idle priority with a copy of
// the track taken when it was asked for.

static Track retimeTrack;  // lines and names only, no map
static String retimeFile;
static std::atomic<bool> retimeRunning{false};
static std::mutex retimeLock;  // retimeState: written by the task, read by the web server
static RetimeStatus retimeState;

static void setRetimeState(int done, int total, int laps, const String& message, bool running) {
  std::lock_guard<std::mutex> g(retimeLock);
  retimeState.done = done;
  retimeState.total = total;
  retimeState.laps = laps;
  retimeState.running = running;
  retimeState.message = message;
}

bool retiming() { return retimeRunning; }

RetimeStatus retimeStatus() {
  std::lock_guard<std::mutex> g(retimeLock);
  return retimeState;
}

// The best lap of a re-timed session, with its path, as a best-lap record (lapMs 0 if none).
static bestlap::Record recordOf(const SessionReplay& r, const Track& t) {
  bestlap::Record rec;
  const Lap* best = r.timer.bestLap();
  if (!best || r.timer.bestPathLapIndex() != r.timer.bestLapIndex()) return rec;  // no clean lap with its path
  rec.lapMs = best->timeMs;
  rec.date = bestlap::dateFromStamp(r.stamp);
  rec.sfLat = t.startFinish.lat;
  rec.sfLon = t.startFinish.lon;
  rec.path.reserve(r.timer.bestPath().size());
  for (const LapTimer::TracePt& p : r.timer.bestPath()) rec.path.push_back({p.x, p.y, p.t, p.speedKmh});
  return rec;
}

static void retimeAll(fs::FS& fs) {
  const Track& t = retimeTrack;
  const std::string file = retimeFile.c_str(), trackId = t.id.c_str();
  std::vector<String> logs;
  File dir = fs.open(SESSION_DIR);
  if (dir && dir.isDirectory()) {
    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
      String name = fileName(e.name());
      if (!e.isDirectory() && name.endsWith(".vbo")) logs.push_back(String(SESSION_DIR) + "/" + name);
      e.close();
    }
    dir.close();
  }
  std::vector<String> chosen;
  for (const String& log : logs) {
    const String base = withoutExt(log, ".vbo");
    if (retime::wanted(fileName(log).c_str(), summaryTrack(fs, base + ".json"), file, trackId)) chosen.push_back(log);
  }
  if (chosen.empty()) {
    setRetimeState(0, 0, 0, file.size() ? "That session's log isn't on the SD card" : "No logged sessions at this track",
                   false);
    return;
  }

  // Each session's best lap as it's re-timed. The quickest, and the latest
  // day's quickest, are kept with their paths for the best-lap files.
  std::vector<retime::Found> found;
  bestlap::Record pb, day;
  int laps = 0;
  for (size_t i = 0; i < chosen.size(); i++) {
    const String& log = chosen[i];
    const String base = withoutExt(log, ".vbo");
    setRetimeState((int)i, (int)chosen.size(), laps, "Re-timing " + fileName(log), true);
    File f = fs.open(log, "r");
    if (!f) continue;
    std::unique_ptr<SessionReplay> replay(new SessionReplay());
    replay->summaryTrackId = trackId;
    replay->useLines(t.startFinish, t.splits, t.sectors);
    if (t.stage) replay->useStage(t.finish, t.hasFinish);
    static char chunk[4096];
    for (;;) {
      int n = f.read((uint8_t*)chunk, sizeof(chunk));
      if (n <= 0) break;
      replay->feed(chunk, n);
    }
    f.close();
    replay->finish();
    if (replay->freeRoam) continue;

    save(base + ".json", t, replay->stamp.c_str(), replay->session, replay->timer, fileName(log), true);
    laps += (int)replay->timer.lapCount();

    bestlap::Record rec = recordOf(*replay, t);
    retime::Found fd;
    fd.lapMs = rec.lapMs;
    fd.date = rec.date;
    fd.session = i;
    found.push_back(fd);
    if (rec.lapMs && (!pb.lapMs || rec.lapMs < pb.lapMs)) pb = rec;
    if (rec.lapMs && (rec.date > day.date || (rec.date == day.date && (!day.lapMs || rec.lapMs < day.lapMs)))) day = rec;
    DEBUG_PRINTF("Re-time: %s: %u laps, best %s\n", fileName(log).c_str(), (unsigned)replay->timer.lapCount(),
                 rec.lapMs ? formatLapTime(rec.lapMs).c_str() : "none");
  }

  // The best laps, by Retime.h's rules
  String what;
  bestlap::Record stored;
  bool have = BestLapStore::load(t.id, BestLapStore::Kind::Ever, stored, false);
  if (retime::replacesPb(retime::best(found), have ? stored.lapMs : 0,
                         have && bestlap::sameLine(stored, t.startFinish.lat, t.startFinish.lon)) &&
      BestLapStore::save(t.id, BestLapStore::Kind::Ever, pb))
    what = ", PB " + formatLapTime(pb.lapMs);
  have = BestLapStore::load(t.id, BestLapStore::Kind::Day, stored, false);
  if (retime::replacesDay(retime::bestOfLatestDay(found), have ? stored.lapMs : 0, have ? stored.date : 0,
                          have && bestlap::sameLine(stored, t.startFinish.lat, t.startFinish.lon)))
    BestLapStore::save(t.id, BestLapStore::Kind::Day, day);
  char msg[96];
  snprintf(msg, sizeof(msg), "Done: %u session%s, %d laps", (unsigned)chosen.size(), chosen.size() == 1 ? "" : "s", laps);
  setRetimeState((int)chosen.size(), (int)chosen.size(), laps, String(msg) + what, false);
  DEBUG_PRINTF("Re-time: %s%s\n", msg, what.c_str());
}

static void retimeTaskMain(void* arg) {
  retimeAll(*(fs::FS*)arg);
  retimeTrack = Track();
  retimeRunning = false;
  vTaskDelete(nullptr);
}

bool retime(const Track& track, const String& logName, String& error) {
  fs::FS* fs = Storage::sessionFS();
  error = "";
  if (!fs || !fullLogAvailable()) error = "Re-timing needs the SD card with the session logs";
  else if (!track.hasStartFinish) error = "This track has no start/finish line";
  else if (logging()) error = "A session is being logged: end it first";
  else if (recoveryRunning) error = "Track Rat is still recovering sessions from the last power cut";
  else if (retimeRunning) error = "Already re-timing";
  if (error.length()) return false;

  retimeTrack = Track();
  retimeTrack.id = track.id;
  retimeTrack.venueId = track.venueId;
  retimeTrack.name = track.name;
  retimeTrack.layoutName = track.layoutName;
  retimeTrack.location = track.location;
  retimeTrack.hasStartFinish = true;
  retimeTrack.startFinish = track.startFinish;
  retimeTrack.splits = track.splits;
  retimeTrack.sectors = track.sectors;
  retimeTrack.stage = track.stage;
  retimeTrack.hasFinish = track.hasFinish;
  retimeTrack.finish = track.finish;
  retimeFile = logName;
  setRetimeState(0, 0, 0, "Starting", true);
  retimeRunning = true;
  if (xTaskCreatePinnedToCore(retimeTaskMain, "retime", 8192, fs, tskIDLE_PRIORITY, nullptr, 0) != pdPASS) {
    retimeRunning = false;
    setRetimeState(0, 0, 0, "Couldn't start", false);
    error = "Not enough memory to start re-timing";
    return false;
  }
  return true;
}

}  // namespace SessionLog
