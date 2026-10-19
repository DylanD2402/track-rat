/**
 * Track Rat - Session log implementation
 */

#include "SessionLog.h"
#include "Config.h"
#include "Storage.h"
#include <ArduinoJson.h>

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

String newSessionPath(const Track& track, const String& stamp) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs) return String();
  if (!fs->exists(SESSION_DIR)) fs->mkdir(SESSION_DIR);

  String base = String(SESSION_DIR) + "/" + (stamp.length() ? stamp : String("session")) + "_" + track.id;
  String path = base + ".json";
  for (int n = 2; fs->exists(path) && n < 1000; n++) {
    path = base + "_" + n + ".json";
  }
  return path;
}

bool save(const String& path, const Track& track, const String& stamp, const LapTimer& timer) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || path.length() == 0) return false;

  DynamicJsonDocument doc(4096 + timer.lapCount() * 256);
  doc["track"] = track.id;
  doc["trackName"] = track.name;
  doc["started"] = stamp;
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

  File f = fs->open(path, "w");
  if (!f) {
    DEBUG_PRINTF("Session: could not open %s for writing\n", path.c_str());
    return false;
  }
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

String saveFreeRoam(const String& stamp, const FreeRoam& drive, uint32_t durationMs) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || !drive.hasPosition()) return String();
  if (!fs->exists(SESSION_DIR)) fs->mkdir(SESSION_DIR);
  String base = String(SESSION_DIR) + "/" + (stamp.length() ? stamp : String("drive")) + "_freeroam";
  String path = base + ".json";
  for (int n = 2; fs->exists(path) && n < 1000; n++) path = base + "_" + n + ".json";

  const size_t step = max((size_t)1, (size_t)(10.0f / FreeRoam::MIN_POINT_SPACING_M));
  const size_t points = drive.path.size() / step + 2;
  DynamicJsonDocument doc(4096 + points * 64);
  doc["type"] = "freeroam";
  doc["started"] = stamp;
  doc["durationMs"] = durationMs;
  doc["duration"] = formatLapTime(durationMs, 0);
  doc["distanceKm"] = serialized(String(drive.distanceKm(), 3));
  doc["topSpeedKmh"] = serialized(String(drive.topSpeedKmh(), 1));
  doc["avgMovingKmh"] = serialized(String(drive.avgMovingKmh(), 1));
  JsonArray route = doc.createNestedArray("route");
  for (size_t i = 0; i < drive.path.size(); i += step) {
    double lat, lon;
    geo::fromLocal(drive.path[i].x, drive.path[i].y, drive.refLat, drive.refLon, lat, lon);
    JsonArray p = route.createNestedArray();
    p.add(serialized(String(lat, 6)));
    p.add(serialized(String(lon, 6)));
  }

  File f = fs->open(path, "w");
  if (!f) return String();
  serializeJson(doc, f);
  f.close();
  DEBUG_PRINTF("Session: free roam saved to %s:%s\n", Storage::sessionFSName(), path.c_str());
  return path;
}

}  // namespace SessionLog
