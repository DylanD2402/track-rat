/**
 * Track Rat - Track Database Implementation
 */

#include <ArduinoJson.h>
#include "TrackDatabase.h"
#include "Config.h"
#include "Storage.h"
#include <LittleFS.h>
#include <SD_MMC.h>

// tracks.json is replaced by removing it and renaming the new copy into place
// (FAT can't rename over a file): editFile() writes tracks.json.tmp, the
// Wi-Fi page moves the old one to tracks.json.bak first. A power cut between
// the two steps leaves no tracks.json; put the complete copy back.
static void recoverTracksFile(fs::FS& fs, const char* name) {
  if (fs.exists(TRACK_DATABASE_FILE)) return;
  const String candidates[] = {String(TRACK_DATABASE_FILE) + ".tmp", String(TRACK_DATABASE_FILE) + ".bak"};
  for (const String& c : candidates) {
    if (fs.exists(c) && fs.rename(c, TRACK_DATABASE_FILE)) {
      DEBUG_PRINTF("Tracks: %s%s was missing - restored it from %s\n", name, TRACK_DATABASE_FILE, c.c_str());
      return;
    }
  }
}

void TrackDatabase::begin() {
  if (Storage::sdMounted()) recoverTracksFile(SD_MMC, "SD");
  if (Storage::flashMounted()) recoverTracksFile(LittleFS, "flash");
  if (Storage::sdMounted() && loadFrom(SD_MMC, "SD")) return;
  if (Storage::flashMounted() && loadFrom(LittleFS, "flash")) return;
  loadBuiltIn();
}

static void readPoint(JsonObject j, TrackPoint& p) {
  p.lat = j["lat"] | 0.0;
  p.lon = j["lon"] | 0.0;
  p.radius = j["radius"] | DEFAULT_GATE_HALF_WIDTH_M;
  p.heading = 0;
  p.headingFrom = HeadingFrom::None;
  if (j["heading"].is<float>()) {  // any number; a string or other junk means no direction
    p.heading = fmodf(fmodf(j["heading"].as<float>(), 360.0f) + 360.0f, 360.0f);
    p.headingFrom = HeadingFrom::Set;
  }
}

// Start/finish, splits and map of one layout, or of a venue without layouts.
static void readLayout(JsonObject src, Track& track) {
  readPoint(src["startFinish"], track.startFinish);
  track.hasStartFinish = track.startFinish.lat != 0.0 || track.startFinish.lon != 0.0;
  track.hasMap = src["map"].as<JsonArray>().size() > 0;
  for (JsonObject s : src["splits"].as<JsonArray>()) {
    TrackPoint p;
    readPoint(s, p);
    track.splits.push_back(p);
  }

  // No centre: use the start/finish, or else the middle of the map
  if (track.centerLat == 0.0 && track.centerLon == 0.0) {
    if (track.hasStartFinish) {
      track.centerLat = track.startFinish.lat;
      track.centerLon = track.startFinish.lon;
    } else if (track.hasMap) {
      JsonArray first = src["map"][0];
      if (first.size()) {
        track.centerLat = first[first.size() / 2][0] | 0.0;
        track.centerLon = first[first.size() / 2][1] | 0.0;
      }
    }
  }
}

// "Short Course" -> "short-course"
static String slug(const String& name) {
  String out;
  for (size_t i = 0; i < name.length(); i++) {
    unsigned char c = tolower((unsigned char)name[i]);
    if (isalnum(c)) out += (char)c;
    else if (out.length() && out[out.length() - 1] != '-') out += '-';
  }
  while (out.endsWith("-")) out.remove(out.length() - 1);
  return out;
}

// One entry of tracks.json: a Track per layout, or one if it has no "layouts".
static void readEntry(JsonObject t, std::vector<Track>& out) {
  Track venue;
  venue.venueId = t["id"] | "";
  venue.id = venue.venueId;
  venue.name = t["name"] | "Unnamed";
  venue.location = t["location"] | "";
  venue.country = t["country"] | "";
  venue.length = t["length"] | 0.0f;
  venue.centerLat = t["center"]["lat"] | 0.0;
  venue.centerLon = t["center"]["lon"] | 0.0;

  JsonArray layouts = t["layouts"];
  if (layouts.size() == 0) {
    readLayout(t, venue);
    out.push_back(venue);
    return;
  }
  int i = 0;
  size_t first = out.size();
  for (JsonObject lo : layouts) {
    Track track = venue;
    track.layoutIndex = i++;
    track.layoutName = lo["name"] | "";
    if (!track.layoutName.length()) track.layoutName = String("Layout ") + i;
    String layoutId = lo["id"] | "";
    if (!layoutId.length()) layoutId = slug(track.layoutName);
    if (!layoutId.length()) layoutId = String(i);
    track.id = venue.venueId + "." + layoutId;
    for (size_t k = first; k < out.size(); k++)  // two layouts with the same id would share records
      if (out[k].id == track.id) track.id += String("-") + i;
    track.length = lo["length"] | venue.length;
    readLayout(lo, track);
    out.push_back(track);
  }
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
  int withMap = 0, noLine = 0, layouts = 0;
  std::vector<Track> entry;
  for (JsonObject t : doc["tracks"].as<JsonArray>()) {
    entry.clear();
    readEntry(t, entry);
    for (Track& track : entry) {
      if (track.centerLat == 0.0 && track.centerLon == 0.0) {
        DEBUG_PRINTF("Tracks: skipping '%s' (no location)\n", track.displayName().c_str());
        continue;
      }
      withMap += track.hasMap;
      noLine += !track.hasStartFinish;
      layouts += track.layoutIndex >= 0;
      tracks.push_back(track);
    }
  }

  if (tracks.empty()) return false;
  sourceName = name;
  fileFs = &fs;
  DEBUG_PRINTF("Tracks: loaded %d from %s (%d are layouts of a venue, %d with a map, %d without a start/finish yet)\n",
               tracks.size(), name, layouts, withMap, noLine);
  return true;
}

const Track* TrackDatabase::get(int index) const {
  if (index < 0 || index >= (int)tracks.size()) return nullptr;
  return &tracks[index];
}

const Track* TrackDatabase::find(const String& id) const {
  for (const Track& t : tracks)
    if (t.id == id) return &t;
  return nullptr;
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

bool TrackDatabase::readFile(std::function<void(JsonArray)> use) {
  if (!fileFs) return false;
  File file = fileFs->open(TRACK_DATABASE_FILE, "r");
  if (!file) return false;
  DynamicJsonDocument doc(max((size_t)16384, (size_t)file.size() * 4));
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) return false;
  use(doc["tracks"].as<JsonArray>());
  return true;
}

static JsonObject findById(JsonArray arr, const String& id) {
  for (JsonObject o : arr)
    if (id == (o["id"] | "")) return o;
  return JsonObject();
}

// The object holding a track's startFinish, splits and map: its entry in
// tracks.json, or one of that entry's "layouts".
static JsonObject findLayout(JsonArray arr, const Track& t) {
  JsonObject o = findById(arr, t.venueId);
  if (o.isNull() || t.layoutIndex < 0) return o;
  return o["layouts"][t.layoutIndex].as<JsonObject>();
}

// "map": [ [[lat,lon],[lat,lon],...], ... ]  one array per line to draw
static void readMapLines(JsonObject src, double refLat, double refLon, layout::Lines& out) {
  out.clear();
  for (JsonArray line : src["map"].as<JsonArray>()) {
    std::vector<geo::XYf> pts;
    pts.reserve(line.size());
    for (JsonArray ll : line) {
      geo::XY p = geo::toLocal(ll[0] | 0.0, ll[1] | 0.0, refLat, refLon);
      pts.push_back({(float)p.x, (float)p.y});
    }
    if (pts.size() >= 2) out.push_back(std::move(pts));
  }
}

void TrackDatabase::loadMap(int index) {
  for (int i = 0; i < (int)tracks.size(); i++)
    if (i != index && !tracks[i].bench) {
      tracks[i].map.clear();
      tracks[i].map.shrink_to_fit();
    }
  Track* t = index >= 0 && index < (int)tracks.size() ? &tracks[index] : nullptr;
  if (!t || t->bench || !t->hasMap || !fileFs) return;

  layout::Lines map;
  readFile([&](JsonArray arr) {
    JsonObject o = findLayout(arr, *t);
    if (!o.isNull()) readMapLines(o, t->refLat(), t->refLon(), map);
  });
  setLoadedMap(index, std::move(map));
}

void TrackDatabase::setLoadedMap(int index, layout::Lines&& map) {
  for (int i = 0; i < (int)tracks.size(); i++)
    if (i != index && !tracks[i].bench) {
      tracks[i].map.clear();
      tracks[i].map.shrink_to_fit();
    }
  Track* t = index >= 0 && index < (int)tracks.size() ? &tracks[index] : nullptr;
  if (!t || t->bench) return;
  t->map = std::move(map);

  // Lines with no direction: take it from the map, if it passes close by
  if (t->hasStartFinish && !t->map.empty()) {
    std::vector<geo::XY> splitAt;
    for (const TrackPoint& sp : t->splits) splitAt.push_back(geo::toLocal(sp.lat, sp.lon, t->refLat(), t->refLon()));
    layout::guessHeadings(t->map, {0, 0}, t->startFinish, splitAt, t->splits);
  }
  DEBUG_PRINTF("Tracks: map for '%s': %d line(s)%s\n", t->displayName().c_str(), (int)t->map.size(),
               t->startFinish.headingFrom == HeadingFrom::Map ? ", start/finish direction taken from it" : "");
}

void TrackDatabase::keepGuessedHeadings(int index, const TrackPoint& startFinish, const std::vector<TrackPoint>& splits) {
  if (index < 0 || index >= (int)tracks.size()) return;
  Track& t = tracks[index];
  if (t.startFinish.headingFrom == HeadingFrom::Map && startFinish.headingFrom == HeadingFrom::Map)
    t.startFinish.heading = startFinish.heading;
  for (size_t i = 0; i < t.splits.size() && i < splits.size(); i++)
    if (t.splits[i].headingFrom == HeadingFrom::Map && splits[i].headingFrom == HeadingFrom::Map)
      t.splits[i].heading = splits[i].heading;
}

int TrackDatabase::layoutCount(int index) const {
  const Track* t = get(index);
  if (!t || t->layoutIndex < 0) return 1;
  int n = 0;
  for (const Track& o : tracks) n += o.layoutIndex >= 0 && o.venueId == t->venueId;
  return n;
}

int TrackDatabase::layoutNumber(int index) const {
  const Track* t = get(index);
  if (!t || t->layoutIndex < 0) return 0;
  int n = 0;
  for (int i = 0; i < index; i++) n += tracks[i].layoutIndex >= 0 && tracks[i].venueId == t->venueId;
  return n;
}

int TrackDatabase::nextLayout(int index) const {
  const Track* t = get(index);
  if (!t || t->layoutIndex < 0) return index;
  int n = tracks.size();
  for (int k = 1; k < n; k++) {
    int i = (index + k) % n;
    if (tracks[i].layoutIndex >= 0 && tracks[i].venueId == t->venueId) return i;
  }
  return index;
}

std::vector<int> TrackDatabase::layoutCandidates(int index, double maxDistanceM) const {
  std::vector<int> out;
  const Track* cur = get(index);
  if (!cur || cur->bench) return out;
  for (int i = 0; i < (int)tracks.size(); i++) {
    const Track& t = tracks[i];
    if (i == index || t.bench || !t.hasMap || !t.hasStartFinish) continue;
    if (geo::distanceM(cur->refLat(), cur->refLon(), t.refLat(), t.refLon()) <= maxDistanceM) out.push_back(i);
  }
  return out;
}

bool TrackDatabase::readMaps(const std::vector<int>& indices, double refLat, double refLon,
                             std::vector<layout::Lines>& out) {
  out.assign(indices.size(), layout::Lines());
  return readFile([&](JsonArray arr) {
    for (size_t k = 0; k < indices.size(); k++) {
      const Track* t = get(indices[k]);
      JsonObject o = t ? findLayout(arr, *t) : JsonObject();
      if (!o.isNull()) readMapLines(o, refLat, refLon, out[k]);
    }
  });
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

static void writePoint(JsonObject o, const TrackPoint& p) {
  o["lat"] = serialized(String(p.lat, 7));
  o["lon"] = serialized(String(p.lon, 7));
  o["radius"] = p.radius;
  if (p.headingFrom == HeadingFrom::Set) o["heading"] = serialized(String(p.heading, 1));  // guesses aren't saved
}

// A new entry in tracks.json: the venue, plus its start/finish if it has no layouts.
static void writeTrack(JsonObject o, const Track& t) {
  o["id"] = t.venueId;
  o["name"] = t.name;
  o["location"] = t.location;
  o["country"] = t.country;
  o["length"] = t.length;
  o["center"]["lat"] = serialized(String(t.centerLat, 7));
  o["center"]["lon"] = serialized(String(t.centerLon, 7));
  if (t.hasStartFinish && t.layoutIndex < 0) writePoint(o.createNestedObject("startFinish"), t.startFinish);
}

// Like findLayout, but adds the entry or layout if it isn't in the file.
static JsonObject findOrAddLayout(JsonArray arr, const Track& t) {
  JsonObject o = findById(arr, t.venueId);
  if (o.isNull()) {
    o = arr.createNestedObject();
    writeTrack(o, t);
  }
  if (t.layoutIndex < 0) return o;
  JsonArray layouts = o["layouts"].isNull() ? o.createNestedArray("layouts") : o["layouts"].as<JsonArray>();
  while ((int)layouts.size() <= t.layoutIndex) layouts.createNestedObject();
  JsonObject lo = layouts[t.layoutIndex];
  if (lo["name"].isNull()) lo["name"] = t.layoutName;
  return lo;
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

int TrackDatabase::createTrack(double lat, double lon) {
  int n = 1;
  for (const Track& t : tracks)
    if (t.id.startsWith("my-track-")) n = max(n, (int)t.id.substring(9).toInt() + 1);

  Track t;
  t.id = String("my-track-") + n;
  t.venueId = t.id;
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

bool TrackDatabase::setStartFinish(int index, double lat, double lon, float headingDeg) {
  if (index < 0 || index >= (int)tracks.size()) return false;
  Track& t = tracks[index];
  t.startFinish = {lat, lon, t.startFinish.radius > 0 ? t.startFinish.radius : (float)DEFAULT_GATE_HALF_WIDTH_M};
  if (headingDeg >= 0) {
    t.startFinish.heading = headingDeg;
    t.startFinish.headingFrom = HeadingFrom::Set;
  }
  for (TrackPoint& sp : t.splits)  // guessed to match the old line: guess again below (loadMap)
    if (sp.headingFrom == HeadingFrom::Map) sp.headingFrom = HeadingFrom::None;
  t.hasStartFinish = true;
  if (t.centerLat == 0.0 && t.centerLon == 0.0) {
    t.centerLat = lat;
    t.centerLon = lon;
  }
  DEBUG_PRINTF("Tracks: start/finish for '%s' set to %.7f, %.7f, direction %s\n", t.displayName().c_str(), lat, lon,
               headingDeg >= 0 ? String(headingDeg, 0).c_str() : "none");
  if (t.bench) return true;

  bool ok = editFile(1024, [&](JsonArray arr) {
    JsonObject o = findOrAddLayout(arr, t);
    writePoint(o.createNestedObject("startFinish"), t.startFinish);
    JsonObject venue = findById(arr, t.venueId);
    if (venue["center"].isNull()) {
      venue["center"]["lat"] = serialized(String(t.centerLat, 7));
      venue["center"]["lon"] = serialized(String(t.centerLon, 7));
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
    JsonObject o = findOrAddLayout(arr, t);
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
  DEBUG_PRINTF("Tracks: map for '%s' %s (%d points)\n", t.displayName().c_str(), ok ? "saved" : "NOT saved",
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
    t.venueId = b.id;
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
