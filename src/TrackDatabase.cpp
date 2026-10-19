/**
 * Track Rat - Track Database Implementation
 */

#include <ArduinoJson.h>
#include "TrackDatabase.h"
#include "Config.h"
#include "Storage.h"
#include <LittleFS.h>
#include <SD_MMC.h>

void TrackDatabase::begin() {
  if (Storage::sdMounted() && loadFrom(SD_MMC, "SD")) return;
  if (Storage::flashMounted() && loadFrom(LittleFS, "flash")) return;
  loadBuiltIn();
}

static bool readTrack(JsonObject t, Track& track) {
  track.id = t["id"] | "";
  track.name = t["name"] | "Unnamed";
  track.location = t["location"] | "";
  track.country = t["country"] | "";
  track.length = t["length"] | 0.0f;
  track.startFinish.lat = t["startFinish"]["lat"] | 0.0;
  track.startFinish.lon = t["startFinish"]["lon"] | 0.0;
  track.startFinish.radius = t["startFinish"]["radius"] | DEFAULT_GATE_HALF_WIDTH_M;
  track.hasStartFinish = track.startFinish.lat != 0.0 || track.startFinish.lon != 0.0;
  track.centerLat = t["center"]["lat"] | track.startFinish.lat;
  track.centerLon = t["center"]["lon"] | track.startFinish.lon;
  track.hasMap = t["map"].as<JsonArray>().size() > 0;

  // No centre or start/finish: use the middle of the map, if there is one
  if (track.centerLat == 0.0 && track.centerLon == 0.0 && track.hasMap) {
    JsonArray first = t["map"][0];
    if (first.size()) {
      track.centerLat = first[first.size() / 2][0] | 0.0;
      track.centerLon = first[first.size() / 2][1] | 0.0;
    }
  }

  for (JsonObject s : t["splits"].as<JsonArray>()) {
    TrackPoint p;
    p.lat = s["lat"] | 0.0;
    p.lon = s["lon"] | 0.0;
    p.radius = s["radius"] | DEFAULT_GATE_HALF_WIDTH_M;
    track.splits.push_back(p);
  }
  return track.centerLat != 0.0 || track.centerLon != 0.0;
}

bool TrackDatabase::loadFrom(fs::FS& fs, const char* name) {
  if (!fs.exists(TRACK_DATABASE_FILE)) return false;
  File file = fs.open(TRACK_DATABASE_FILE, "r");
  if (!file) return false;

  DynamicJsonDocument doc(max((size_t)16384, (size_t)file.size() * 4));  // large: lands in PSRAM
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    DEBUG_PRINTF("Tracks: %s%s is invalid JSON: %s\n", name, TRACK_DATABASE_FILE, err.c_str());
    return false;
  }

  tracks.clear();
  int withMap = 0, noLine = 0;
  for (JsonObject t : doc["tracks"].as<JsonArray>()) {
    Track track;
    if (!readTrack(t, track)) {
      DEBUG_PRINTF("Tracks: skipping '%s' (no location)\n", track.name.c_str());
      continue;
    }
    withMap += track.hasMap;
    noLine += !track.hasStartFinish;
    tracks.push_back(track);
  }

  if (tracks.empty()) return false;
  sourceName = name;
  fileFs = &fs;
  DEBUG_PRINTF("Tracks: loaded %d from %s (%d with a map, %d without a start/finish yet)\n", tracks.size(), name,
               withMap, noLine);
  return true;
}

const Track* TrackDatabase::get(int index) const {
  if (index < 0 || index >= (int)tracks.size()) return nullptr;
  return &tracks[index];
}

int TrackDatabase::findNearest(double lat, double lon, double maxDistanceM, double* distanceOut) const {
  int best = -1;
  double bestDist = maxDistanceM;
  for (size_t i = 0; i < tracks.size(); i++) {
    if (tracks[i].bench) continue;
    double d = geo::distanceM(lat, lon, tracks[i].refLat(), tracks[i].refLon());
    if (d < bestDist) {
      bestDist = d;
      best = i;
    }
  }
  if (distanceOut) *distanceOut = bestDist;
  return best;
}

void TrackDatabase::loadMap(int index) {
  for (int i = 0; i < (int)tracks.size(); i++)
    if (i != index && !tracks[i].bench) {
      tracks[i].map.clear();
      tracks[i].map.shrink_to_fit();
    }
  Track* t = index >= 0 && index < (int)tracks.size() ? &tracks[index] : nullptr;
  if (!t || t->bench || !t->hasMap || !fileFs) return;

  File file = fileFs->open(TRACK_DATABASE_FILE, "r");
  if (!file) return;
  DynamicJsonDocument doc(max((size_t)16384, (size_t)file.size() * 4));
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) return;

  t->map.clear();
  for (JsonObject j : doc["tracks"].as<JsonArray>()) {
    if (t->id != (j["id"] | "")) continue;
    // "map": [ [[lat,lon],[lat,lon],...], ... ]  one array per line to draw
    for (JsonArray line : j["map"].as<JsonArray>()) {
      std::vector<geo::XYf> pts;
      pts.reserve(line.size());
      for (JsonArray ll : line) {
        geo::XY p = geo::toLocal(ll[0] | 0.0, ll[1] | 0.0, t->refLat(), t->refLon());
        pts.push_back({(float)p.x, (float)p.y});
      }
      if (pts.size() >= 2) t->map.push_back(std::move(pts));
    }
    break;
  }
  DEBUG_PRINTF("Tracks: map for '%s': %d line(s)\n", t->name.c_str(), (int)t->map.size());
}

// --- writing ----------------------------------------------------------------

fs::FS* TrackDatabase::writableFs() {
  if (fileFs) return fileFs;
  if (Storage::sdMounted()) return &SD_MMC;
  if (Storage::flashMounted()) return &LittleFS;
  return nullptr;
}

int TrackDatabase::insertIndex() const {
  return (!tracks.empty() && tracks.back().bench) ? tracks.size() - 1 : tracks.size();
}

static void writeTrack(JsonObject o, const Track& t) {
  o["id"] = t.id;
  o["name"] = t.name;
  o["location"] = t.location;
  o["country"] = t.country;
  o["length"] = t.length;
  o["center"]["lat"] = serialized(String(t.centerLat, 7));
  o["center"]["lon"] = serialized(String(t.centerLon, 7));
  if (t.hasStartFinish) {
    o["startFinish"]["lat"] = serialized(String(t.startFinish.lat, 7));
    o["startFinish"]["lon"] = serialized(String(t.startFinish.lon, 7));
    o["startFinish"]["radius"] = t.startFinish.radius;
  }
}

// Read tracks.json (or start one from the in-memory list), let `change`
// modify it, and write it back safely (temp file, then rename).
bool TrackDatabase::editFile(size_t extraBytes, std::function<bool(JsonArray)> change) {
  uint32_t startMs = millis();
  fs::FS* fs = writableFs();
  if (!fs) {
    DEBUG_PRINTLN("Tracks: no storage to save to");
    return false;
  }

  size_t fileSize = 0;
  if (fs->exists(TRACK_DATABASE_FILE)) {
    File f = fs->open(TRACK_DATABASE_FILE, "r");
    fileSize = f ? f.size() : 0;
    f.close();
  }
  DynamicJsonDocument doc(16384 + fileSize * 4 + tracks.size() * 512 + extraBytes);

  if (fileSize) {
    File f = fs->open(TRACK_DATABASE_FILE, "r");
    DeserializationError err = f ? deserializeJson(doc, f) : DeserializationError::InvalidInput;
    f.close();
    if (err) {  // never rebuild over an existing file: it may hold maps we haven't loaded
      DEBUG_PRINTF("Tracks: can't read tracks.json to update it (%s) - not saved\n", err.c_str());
      return false;
    }
  } else {  // no file yet (built-in list): start one from what's in memory
    doc.clear();
    JsonArray arr = doc.createNestedArray("tracks");
    for (const Track& t : tracks)
      if (!t.bench) writeTrack(arr.createNestedObject(), t);
  }

  JsonArray arr = doc["tracks"].as<JsonArray>();
  if (arr.isNull()) arr = doc.createNestedArray("tracks");
  if (!change(arr)) return false;
  if (doc.overflowed()) {
    DEBUG_PRINTLN("Tracks: out of memory editing tracks.json - not saved");
    return false;
  }

  String tmp = String(TRACK_DATABASE_FILE) + ".tmp";
  File out = fs->open(tmp, "w");
  if (!out) return false;
  size_t written = serializeJsonPretty(doc, out);
  out.close();
  if (!written) return false;
  fs->remove(TRACK_DATABASE_FILE);
  if (!fs->rename(tmp, TRACK_DATABASE_FILE)) return false;
  fileFs = fs;
  sourceName = (fs == &SD_MMC) ? "SD" : "flash";
  DEBUG_PRINTF("Tracks: saved tracks.json (%u KB) in %lu ms\n", (unsigned)(written / 1024),
               (unsigned long)(millis() - startMs));
  return true;
}

static JsonObject findById(JsonArray arr, const String& id) {
  for (JsonObject o : arr)
    if (id == (o["id"] | "")) return o;
  return JsonObject();
}

int TrackDatabase::createTrack(double lat, double lon) {
  int n = 1;
  for (const Track& t : tracks)
    if (t.id.startsWith("my-track-")) n = max(n, (int)t.id.substring(9).toInt() + 1);

  Track t;
  t.id = String("my-track-") + n;
  t.name = String("My Track ") + n;
  t.location = "Created on the timer";
  t.centerLat = lat;
  t.centerLon = lon;

  bool ok = editFile(1024, [&](JsonArray arr) {
    writeTrack(arr.createNestedObject(), t);
    return true;
  });
  if (!ok) DEBUG_PRINTLN("Tracks: new track not saved (kept for this session only)");
  int idx = insertIndex();
  tracks.insert(tracks.begin() + idx, t);
  DEBUG_PRINTF("Tracks: created '%s' at %.6f, %.6f\n", t.name.c_str(), lat, lon);
  return idx;
}

bool TrackDatabase::setStartFinish(int index, double lat, double lon) {
  if (index < 0 || index >= (int)tracks.size()) return false;
  Track& t = tracks[index];
  t.startFinish = {lat, lon, t.startFinish.radius > 0 ? t.startFinish.radius : (float)DEFAULT_GATE_HALF_WIDTH_M};
  t.hasStartFinish = true;
  if (t.centerLat == 0.0 && t.centerLon == 0.0) {
    t.centerLat = lat;
    t.centerLon = lon;
  }
  DEBUG_PRINTF("Tracks: start/finish for '%s' set to %.7f, %.7f\n", t.name.c_str(), lat, lon);
  if (t.bench) return true;

  bool ok = editFile(1024, [&](JsonArray arr) {
    JsonObject o = findById(arr, t.id);
    if (o.isNull()) writeTrack(arr.createNestedObject(), t);
    else {
      o["startFinish"]["lat"] = serialized(String(lat, 7));
      o["startFinish"]["lon"] = serialized(String(lon, 7));
      o["startFinish"]["radius"] = t.startFinish.radius;
      if (o["center"].isNull()) {
        o["center"]["lat"] = serialized(String(t.centerLat, 7));
        o["center"]["lon"] = serialized(String(t.centerLon, 7));
      }
    }
    return true;
  });
  loadMap(index);  // the map is stored relative to the start/finish now
  return ok;
}

bool TrackDatabase::saveMap(int index, const std::vector<geo::XYf>& line) {
  if (index < 0 || index >= (int)tracks.size() || line.size() < 2) return false;
  Track& t = tracks[index];
  t.map.clear();
  t.map.push_back(line);
  t.hasMap = true;
  if (t.bench) return true;

  bool ok = editFile(line.size() * 64, [&](JsonArray arr) {
    JsonObject o = findById(arr, t.id);
    if (o.isNull()) {
      o = arr.createNestedObject();
      writeTrack(o, t);
    }
    JsonArray m = o.createNestedArray("map");  // replaces any existing map
    JsonArray l = m.createNestedArray();
    for (const geo::XYf& p : line) {
      double lat, lon;
      geo::fromLocal(p.x, p.y, t.refLat(), t.refLon(), lat, lon);
      JsonArray pt = l.createNestedArray();
      pt.add(serialized(String(lat, 6)));
      pt.add(serialized(String(lon, 6)));
    }
    return true;
  });
  DEBUG_PRINTF("Tracks: map for '%s' %s (%d points)\n", t.name.c_str(), ok ? "saved" : "NOT saved",
               (int)line.size());
  return ok;
}

// Fallback list, used when no tracks.json is found. Coordinates are
// approximate - verify the start/finish position before relying on them.
void TrackDatabase::loadBuiltIn() {
  struct Builtin {
    const char* id;
    const char* name;
    const char* location;
    const char* country;
    float length;
    double lat, lon;
  };
  static const Builtin list[] = {
    {"laguna-seca", "Laguna Seca", "Monterey, CA", "USA", 3602, 36.584722, -121.753889},
    {"nurburgring-gp", "Nurburgring GP", "Nurburg", "Germany", 5148, 50.335278, 6.947222},
    {"silverstone", "Silverstone", "Silverstone", "UK", 5891, 52.070278, -1.016667},
    {"cota", "Circuit of the Americas", "Austin, TX", "USA", 5513, 30.132778, -97.641111},
    {"spa", "Spa-Francorchamps", "Stavelot", "Belgium", 7004, 50.437222, 5.971389},
    {"monza", "Monza", "Monza", "Italy", 5793, 45.620556, 9.289167},
    {"suzuka", "Suzuka", "Suzuka", "Japan", 5807, 34.843056, 136.540833},
  };

  tracks.clear();
  for (const Builtin& b : list) {
    Track t;
    t.id = b.id;
    t.name = b.name;
    t.location = b.location;
    t.country = b.country;
    t.length = b.length;
    t.startFinish = {b.lat, b.lon, (float)DEFAULT_GATE_HALF_WIDTH_M};
    t.hasStartFinish = true;
    t.centerLat = b.lat;
    t.centerLon = b.lon;
    tracks.push_back(t);
  }
  sourceName = "built-in";
  fileFs = nullptr;
  DEBUG_PRINTF("Tracks: no tracks.json found, using %d built-in tracks\n", tracks.size());
}
