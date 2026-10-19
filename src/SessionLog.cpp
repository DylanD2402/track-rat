/**
 * Track Rat - Session log implementation
 */

#include "SessionLog.h"
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include "Config.h"
#include "LogWriter.h"
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

String newSessionBase(const String& stamp, const String& name) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs) return String();
  if (!fs->exists(SESSION_DIR)) fs->mkdir(SESSION_DIR);

  String first = String(SESSION_DIR) + "/" + (stamp.length() ? stamp : String("session")) + "_" + name;
  String base = first;
  for (int n = 2; (fs->exists(base + ".json") || fs->exists(base + ".vbo")) && n < 1000; n++) {
    base = first + "_" + n;
  }
  return base;
}

// Write to a temporary file, then rename it over the old one, so a power
// cut leaves either the old summary or the new one, never half of one.
static bool writeJson(fs::FS& fs, const String& path, const JsonDocument& doc, bool pretty) {
  String tmp = path + ".tmp";
  File f = fs.open(tmp, "w");
  if (!f) {
    DEBUG_PRINTF("Session: could not open %s for writing\n", tmp.c_str());
    return false;
  }
  size_t n = pretty ? serializeJsonPretty(doc, f) : serializeJson(doc, f);
  f.close();
  if (!n) {
    fs.remove(tmp);
    return false;
  }
  if (fs.rename(tmp, path)) return true;  // LittleFS replaces the old file in one step
  fs.remove(path);                        // FAT won't rename over an existing file
  return fs.rename(tmp, path);
}

bool save(const String& path, const Track& track, const String& stamp, const LapTimer& timer,
          const String& logName, bool finished, bool recovered) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || path.length() == 0) return false;

  DynamicJsonDocument doc(4096 + timer.lapCount() * 256);
  doc["track"] = track.id;
  doc["trackName"] = track.name;
  doc["started"] = stamp;
  if (logName.length()) doc["log"] = logName;
  doc["finished"] = finished;
  if (recovered) doc["recovered"] = true;
  doc["lapCount"] = timer.lapCount();
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
    if (!l.splitMs.empty()) {
      JsonArray s = o.createNestedArray("splitsMs");
      for (int32_t v : l.splitMs) s.add(v);
    }
  }
  return writeJson(*fs, path, doc, true);
}

bool saveFreeRoam(const String& path, const String& stamp, const FreeRoam& drive, uint32_t durationMs,
                  const String& logName, bool recovered) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || path.length() == 0 || (!drive.hasPosition() && logName.length() == 0)) return false;

  const bool withRoute = logName.length() == 0;  // the full log has the route at every fix
  const size_t step = max((size_t)1, (size_t)(10.0f / FreeRoam::MIN_POINT_SPACING_M));
  const size_t points = withRoute ? drive.path.size() / step + 2 : 0;
  DynamicJsonDocument doc(4096 + points * 64);
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
  if (!writeJson(*fs, path, doc, false)) return false;
  DEBUG_PRINTF("Session: free roam saved to %s:%s\n", Storage::sessionFSName(), path.c_str());
  return true;
}

// --- Full-rate log ----------------------------------------------------------

static bool logOpen = false;
static String logPath;
static uint32_t fixCount = 0;
static uint32_t fixesMissed = 0;
static vbo::GForce gforce;

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
  std::string text = vbo::headerText(h);
  if (!LogWriter::open(path, text.data(), text.size())) return false;
  logOpen = true;
  logPath = path;
  fixCount = fixesMissed = 0;
  gforce.reset();
  return true;
}

bool beginTrackLog(const String& vboPath, const Track& t, const String& stamp, bool timing, uint32_t minLapMs,
                   const GpsFix& first) {
  vbo::Header h = headerFor(stamp, first);
  char buf[96];
  h.comments.push_back("Type: laps");
  h.comments.push_back(std::string("Track: ") + t.id.c_str());
  h.comments.push_back(std::string("Track name: ") + t.name.c_str());
  if (timing) {
    // Exactly what the lap timer uses, so the session can be timed again from the log
    snprintf(buf, sizeof(buf), "Start/finish: %.7f %.7f %.1f", t.startFinish.lat, t.startFinish.lon,
             t.startFinish.radius);
    h.comments.push_back(buf);
    for (size_t i = 0; i < t.splits.size(); i++) {
      snprintf(buf, sizeof(buf), "Split %u: %.7f %.7f %.1f", (unsigned)(i + 1), t.splits[i].lat, t.splits[i].lon,
               t.splits[i].radius);
      h.comments.push_back(buf);
    }
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
    }
  }
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
  s.hdop = f.pdop;  // PDOP from NAV-PVT, HDOP with an NMEA-only GPS
  s.lap = lap;
  s.latG = gforce.lateral();
  s.longG = gforce.longitudinal();
  char line[128];
  size_t n = vbo::formatLine(s, line, sizeof(line));
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
static void recoverSession(fs::FS& fs, const String& vboPath, const TrackDatabase& db) {
  File f = fs.open(vboPath, "r");
  if (!f) return;
  uint32_t startMs = millis();

  // A log with no start/finish in its header had the line marked during the
  // session: it's in tracks.json now.
  SessionReplay replay([&](const std::string& id, TrackPoint& sf, std::vector<TrackPoint>& splits) {
    const Track* t = db.find(id.c_str());
    if (!t || !t->hasStartFinish) return false;
    sf = t->startFinish;
    splits = t->splits;
    return true;
  });
  static char chunk[4096];
  for (;;) {
    int n = f.read((uint8_t*)chunk, sizeof(chunk));
    if (n <= 0) break;
    replay.feed(chunk, n);
    delay(0);  // a long log takes seconds: let the other tasks on this core run
  }
  f.close();
  replay.finish();

  String base = vboPath.substring(0, vboPath.length() - 4);
  String stamp = replay.stamp.c_str();
  String logName = fileName(vboPath);
  bool ok;
  if (replay.freeRoam) {
    ok = saveFreeRoam(base + ".json", stamp, replay.drive, replay.durationMs, logName, true);
  } else {
    Track track;
    if (const Track* known = db.find(replay.trackId.c_str())) track = *known;
    track.id = replay.trackId.c_str();
    if (replay.trackName.size()) track.name = replay.trackName.c_str();
    ok = save(base + ".json", track, stamp, replay.timer, logName, true, true);
  }
  const Lap* best = replay.timer.bestLap();
  DEBUG_PRINTF("Session: %s %s from its log: %lu fixes, %u laps%s%s, %lu bad lines%s (%lu ms)\n",
               ok ? "recovered" : "could NOT recover", logName.c_str(), (unsigned long)replay.samples,
               (unsigned)replay.timer.lapCount(), best ? ", best " : "", best ? formatLapTime(best->timeMs).c_str() : "",
               (unsigned long)replay.reader().badLines(), replay.cutLine ? ", last line cut short" : "",
               (unsigned long)(millis() - startMs));
}

void recoverUnfinished(const TrackDatabase& tracks) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || !fs->exists(SESSION_DIR)) return;

  std::vector<String> logs, temps;
  File dir = fs->open(SESSION_DIR);
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

  // A summary caught mid-save: keep the new copy if it's complete.
  for (const String& tmp : temps) {
    String json = tmp.substring(0, tmp.length() - 4);
    if (!fs->exists(json) && validJson(*fs, tmp) && fs->rename(tmp, json)) {
      DEBUG_PRINTF("Session: finished saving %s\n", json.c_str());
    } else {
      fs->remove(tmp);
    }
  }

  int recovered = 0;
  for (const String& log : logs) {
    String json = log.substring(0, log.length() - 4) + ".json";
    if (summaryFinished(*fs, json)) continue;
    recoverSession(*fs, log, tracks);
    recovered++;
  }
  if (recovered) DEBUG_PRINTF("Session: %d unfinished session(s) recovered\n", recovered);
}

}  // namespace SessionLog
