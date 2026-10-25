/**
 * Track Rat - Track Database Implementation
 */

#include <ArduinoJson.h>
#include "TrackDatabase.h"
#include "Config.h"
#include "Courses.h"
#include "JsonFile.h"
#include "JsonText.h"
#include "Settings.h"
#include "Storage.h"
#include <LittleFS.h>
#include <SD_MMC.h>
#include <memory>
#include "esp_heap_caps.h"

// True if the file parses as JSON and has a "tracks" array. The filter keeps nothing (the array comes out empty), but
// the whole file's syntax is still checked.
static bool validTracksFile(fs::FS& fs, const String& path) {
  File f = fs.open(path, "r");
  if (!f) return false;
  StaticJsonDocument<64> filter;
  filter["tracks"][0] = false;
  StaticJsonDocument<64> doc;
  DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filter));
  f.close();
  return !err && doc["tracks"].is<JsonArray>();
}

// A save or upload cut off after it removed or moved the old tracks.json leaves the new (or old) list under another
// name and no tracks.json. Put the newest complete one back, or the next edit would start from the built-in list.
// Leftovers next to an existing tracks.json are left alone: .tmp is overwritten by the next save, and an unfinished
// .upload by the next upload (the old file is still the one in use).
static void restoreTracksFile(fs::FS& fs, const char* name) {
  if (fs.exists(TRACK_DATABASE_FILE)) return;
  // Newest first: an on-device save, then an upload that got as far as moving the old file to .bak, then .bak itself
  static const char* const suffixes[] = {".tmp", ".upload", ".bak"};
  for (const char* suffix : suffixes) {
    String path = String(TRACK_DATABASE_FILE) + suffix;
    if (!fs.exists(path) || !validTracksFile(fs, path)) continue;
    if (fs.rename(path, TRACK_DATABASE_FILE)) {
      DEBUG_PRINTF("Tracks: restored %s%s from %s\n", name, TRACK_DATABASE_FILE, path.c_str());
      return;
    }
  }
}

// Set by readEntry when a stage is a course by its "Course 2026-10-20" name alone (made before courses had a
// "kind"): begin() then writes the kind and event into tracks.json, so renaming it later can't lose that
static bool oldCoursesSeen = false;

void TrackDatabase::begin() {
  if (Storage::sdMounted()) restoreTracksFile(SD_MMC, "SD");
  if (Storage::flashMounted()) restoreTracksFile(LittleFS, "flash");
  oldCoursesSeen = false;
  if ((Storage::sdMounted() && loadFrom(SD_MMC, "SD")) || (Storage::flashMounted() && loadFrom(LittleFS, "flash"))) {
    if (oldCoursesSeen) migrateCourses();
    return;
  }
  loadBuiltIn();
}

// Old courses ("Course 2026-10-20" stages with no kind): add "kind": "course" and "event" to tracks.json
void TrackDatabase::migrateCourses() {
  oldCoursesSeen = false;
  const bool ok = editFile(2048, [&](JsonArray arr) {
    bool changed = false;
    for (JsonObject o : arr) {
      if (strcmp(o["type"] | "", "stage") != 0 || !o["kind"].isNull()) continue;
      const courses::Parsed p = courses::classify(true, "", "", o["name"] | "");
      if (!p.course) continue;
      o["kind"] = "course";
      o["event"] = String(p.event.c_str());  // (a String is copied into the document; a const char* is only linked)
      changed = true;
    }
    return changed;
  });
  DEBUG_PRINTF("Tracks: old courses %s\n", ok ? "marked as courses in tracks.json" : "not marked (read-only or no room)");
}

// defaultRadius: for a line with no "radius" (narrower on a kart track)
static void readPoint(JsonObject j, TrackPoint& p, float defaultRadius = DEFAULT_GATE_HALF_WIDTH_M) {
  p.lat = j["lat"] | 0.0;
  p.lon = j["lon"] | 0.0;
  p.radius = j["radius"] | defaultRadius;
  p.heading = 0;
  p.headingFrom = HeadingFrom::None;
  if (j["heading"].is<float>()) {  // any number; a string or other junk means no direction
    p.heading = fmodf(fmodf(j["heading"].as<float>(), 360.0f) + 360.0f, 360.0f);
    p.headingFrom = HeadingFrom::Set;
  }
}

// Start/finish, splits and map of one layout, or of a venue without layouts.
static void readLayout(JsonObject src, Track& track) {
  if (strcmp(src["kind"] | "", "kart") == 0) track.kart = true;  // (or the venue's "kind", or the setting)
  const float gate = settings::defaultGateM(track.kart);
  readPoint(src["startFinish"], track.startFinish, gate);
  track.hasStartFinish = track.startFinish.lat != 0.0 || track.startFinish.lon != 0.0;
  if (strcmp(src["type"] | "", "stage") == 0) track.stage = true;  // (or the venue's "type")
  if (track.stage) {
    readPoint(src["finish"], track.finish, gate);
    track.hasFinish = track.finish.lat != 0.0 || track.finish.lon != 0.0;
  }
  track.hasMap = src["map"].as<JsonArray>().size() > 0;
  // "pit" counts lines of "map"; readMapLines leaves out lines under two points, so count as it does
  uint16_t kept = 0, idx = 0;
  for (JsonArray line : src["map"].as<JsonArray>()) {
    bool pit = false;
    for (JsonVariant v : src["pit"].as<JsonArray>()) pit = pit || (v.is<int>() && v.as<int>() == idx);
    if (line.size() >= 2) {
      if (pit) track.pitLines.push_back(kept);
      kept++;
    }
    idx++;
  }
  for (JsonObject s : src["splits"].as<JsonArray>()) {
    TrackPoint p;
    readPoint(s, p, gate);
    track.splits.push_back(p);
  }
  for (JsonObject s : src["sectors"].as<JsonArray>()) {
    TrackPoint p;
    readPoint(s, p, gate);
    track.sectors.push_back(p);
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

  venue.stage = strcmp(t["type"] | "", "stage") == 0;  // every layout of a stage venue is a stage
  // An autocross course: "kind": "course" and its "event" date, or an old stage named "Course 2026-10-20"
  const courses::Parsed cp = courses::classify(venue.stage, t["kind"] | "", t["event"] | "", std::string(venue.name.c_str()));
  venue.course = cp.course;
  venue.event = cp.event.c_str();
  if (cp.course && !(t["kind"] | "")[0]) oldCoursesSeen = true;
  // A kart track ("kind": "kart", from OSM's karting tag), or every track with Kart everywhere on
  venue.kart = strcmp(t["kind"] | "", "kart") == 0 || Settings::get().kartEverywhere;
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

// Parse tracks.json on `fs` (`where` names it in the log) and hand it to `use`,
// with room for extraBytes more in the document. The file is read in one go
// (into PSRAM if there is some) and parsed in place (ArduinoJson's zero-copy
// mode): the document's strings point into the text instead of being copied,
// and it needs just a slot per value, counted from the text. Strings that
// `use` adds are copied, except const char* ones (literals), which are linked.
static bool withTracksDoc(fs::FS& fs, const char* where, size_t extraBytes, std::function<void(JsonDocument&)> use) {
  size_t capacity = 0;
  for (int attempt = 0; attempt < 3; attempt++) {
    File file = fs.open(TRACK_DATABASE_FILE, "r");
    if (!file) return false;
    size_t size = file.size();
    std::unique_ptr<char, void (*)(void*)> text(
        (char*)heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT), free);  // must outlive the document
    if (!text) text.reset((char*)malloc(size + 1));
    size_t got = text ? file.read((uint8_t*)text.get(), size) : 0;
    file.close();
    if (!text || got != size) {
      DEBUG_PRINTF("Tracks: can't read %s%s (%u bytes)\n", where, TRACK_DATABASE_FILE, (unsigned)size);
      return false;
    }
    text.get()[size] = 0;
    if (!capacity) capacity = JSON_ARRAY_SIZE(jsontext::countValues(text.get(), size)) + extraBytes + 1024;
    DynamicJsonDocument doc(capacity);  // large: lands in PSRAM
    DeserializationError err = deserializeJson(doc, text.get(), size);
    if (!err) {
      use(doc);
      return true;
    }
    if (err != DeserializationError::NoMemory) {
      DEBUG_PRINTF("Tracks: %s%s is invalid JSON: %s\n", where, TRACK_DATABASE_FILE, err.c_str());
      return false;
    }
    DEBUG_PRINTF("Tracks: %s%s needs more than %u bytes of memory - trying again\n", where, TRACK_DATABASE_FILE,
                 (unsigned)capacity);
    capacity *= 2;  // and read the text again: parsing changed it
  }
  return false;
}

// The same, handing `use` the tracks.
static bool withTracksFile(fs::FS& fs, const char* where, std::function<void(JsonArray)> use) {
  return withTracksDoc(fs, where, 0, [&](JsonDocument& doc) { use(doc["tracks"].as<JsonArray>()); });
}

bool TrackDatabase::loadFrom(fs::FS& fs, const char* name) {
  if (!fs.exists(TRACK_DATABASE_FILE)) return false;
  int withMap = 0, noLine = 0, layouts = 0;
  bool read = withTracksFile(fs, name, [&](JsonArray arr) {
    tracks.clear();
    std::vector<Track> entry;
    for (JsonObject t : arr) {
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
  });
  if (!read || tracks.empty()) return false;
  sourceName = name;
  fileFs = &fs;
  DEBUG_PRINTF("Tracks: loaded %d from %s (%d are layouts of a venue, %d with a map, %d without a start/finish yet)\n",
               (int)tracks.size(), name, layouts, withMap, noLine);
  return true;
}

const Track* TrackDatabase::get(int index) const {
  if (index < 0 || index >= (int)tracks.size()) return nullptr;
  return &tracks[index];
}

int TrackDatabase::findNearest(double lat, double lon, double maxDistanceM, double* distanceOut,
                               bool includeCourses) const {
  int best = -1;
  double bestDist = maxDistanceM;
  for (size_t i = 0; i < tracks.size(); i++) {
    if (tracks[i].bench || (tracks[i].course && !includeCourses)) continue;
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
  return fileFs && withTracksFile(*fileFs, sourceName, use);
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
    if (i != index && !tracks[i].bench && !tracks[i].mapUnsaved) {
      tracks[i].map.clear();
      tracks[i].map.shrink_to_fit();
    }
  Track* t = index >= 0 && index < (int)tracks.size() ? &tracks[index] : nullptr;
  if (!t || t->bench || !t->hasMap || (!fileFs && !t->mapUnsaved)) return;

  layout::Lines map;
  if (t->mapUnsaved) {  // not in tracks.json yet
    map.swap(t->map);
  } else {
    readFile([&](JsonArray arr) {
      JsonObject o = findLayout(arr, *t);
      if (!o.isNull()) readMapLines(o, t->refLat(), t->refLon(), map);
    });
  }
  setLoadedMap(index, std::move(map));
}

void TrackDatabase::setLoadedMap(int index, layout::Lines&& map) {
  for (int i = 0; i < (int)tracks.size(); i++)
    if (i != index && !tracks[i].bench && !tracks[i].mapUnsaved) {
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
  if (t.stage) o["type"] = "stage";
  if (t.course) {
    o["kind"] = "course";
    if (t.event.length()) o["event"] = t.event;
  } else if (t.kart && !Settings::get().kartEverywhere) {
    o["kind"] = "kart";
  }
  if (t.stage && t.hasFinish && t.layoutIndex < 0) writePoint(o.createNestedObject("finish"), t.finish);
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

  const char* fsName = (fs == &SD_MMC) ? "SD" : "flash";
  size_t written = 0;
  std::function<void(JsonDocument&)> edit = [&](JsonDocument& doc) {
    JsonArray arr = doc["tracks"].as<JsonArray>();
    if (arr.isNull()) arr = doc.createNestedArray("tracks");
    if (change(arr)) written = writeJsonFileByLine(*fs, TRACK_DATABASE_FILE, doc);
  };

  size_t fileSize = 0;
  if (fs->exists(TRACK_DATABASE_FILE)) {
    File f = fs->open(TRACK_DATABASE_FILE, "r");
    fileSize = f ? f.size() : 0;
    f.close();
  }
  if (fileSize) {
    // Never rebuild over an existing file: it may hold maps we haven't loaded
    if (!withTracksDoc(*fs, fsName, extraBytes, edit)) {
      DEBUG_PRINTLN("Tracks: can't read tracks.json to update it - not saved");
      return false;
    }
  } else {  // no file yet (built-in list): start one from what's in memory
    DynamicJsonDocument doc(16384 + tracks.size() * 512 + extraBytes);
    JsonArray arr = doc.createNestedArray("tracks");
    for (const Track& t : tracks)
      if (!t.bench) writeTrack(arr.createNestedObject(), t);
    edit(doc);
  }

  if (!written) return false;
  fileFs = fs;
  sourceName = fsName;
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
  t.kart = Settings::get().kartEverywhere;
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

int TrackDatabase::createCourse(double lat, double lon, const String& stamp) {
  // "Oct 25 course", and "Oct 25 course 2" for a second that day (Courses.h)
  const std::string event = courses::dateOfStamp(stamp.c_str());
  std::vector<std::string> ids, names;
  for (const Track& o : tracks) {
    ids.push_back(o.venueId.c_str());
    names.push_back(o.name.c_str());
  }
  Track t;
  t.id = courses::courseId(event, ids).c_str();
  t.venueId = t.id;
  t.name = courses::courseName(event, names).c_str();
  t.location = "Created on the timer";
  t.stage = true;
  t.course = true;
  t.event = event.c_str();
  t.kart = Settings::get().kartEverywhere;
  t.startFinish = {lat, lon, settings::defaultGateM(t.kart)};  // no direction: standing starts only
  t.hasStartFinish = true;
  t.centerLat = lat;
  t.centerLon = lon;
  bool ok = editFile(1024, [&](JsonArray arr) {
    writeTrack(arr.createNestedObject(), t);
    return true;
  });
  if (!ok) DEBUG_PRINTLN("Tracks: new course not saved (kept for this session only)");
  int idx = insertIndex();
  tracks.insert(tracks.begin() + idx, t);
  DEBUG_PRINTF("Tracks: created course '%s', start at %.6f, %.6f\n", t.name.c_str(), lat, lon);
  return idx;
}

bool TrackDatabase::setFinish(int index, double lat, double lon, float headingDeg) {
  if (index < 0 || index >= (int)tracks.size()) return false;
  Track& t = tracks[index];
  if (!t.stage) return false;
  t.finish = {lat, lon, t.finish.radius > 0 ? t.finish.radius : settings::defaultGateM(t.kart)};
  if (headingDeg >= 0) {
    t.finish.heading = headingDeg;
    t.finish.headingFrom = HeadingFrom::Set;
  }
  t.hasFinish = true;
  DEBUG_PRINTF("Tracks: finish for '%s' set to %.7f, %.7f, direction %s\n", t.displayName().c_str(), lat, lon,
               headingDeg >= 0 ? String(headingDeg, 0).c_str() : "none");
  return editFile(1024, [&](JsonArray arr) {
    JsonObject o = findOrAddLayout(arr, t);
    o["type"] = "stage";
    writePoint(o.createNestedObject("finish"), t.finish);
    return true;
  });
}

bool TrackDatabase::clearFinish(int index) {
  if (index < 0 || index >= (int)tracks.size()) return false;
  Track& t = tracks[index];
  if (!t.stage) return false;
  t.hasFinish = false;
  t.finish = {0, 0, t.finish.radius};
  DEBUG_PRINTF("Tracks: finish for '%s' taken back\n", t.displayName().c_str());
  return editFile(256, [&](JsonArray arr) {
    JsonObject o = findLayout(arr, t);
    if (o.isNull()) return false;
    o.remove("finish");
    return true;
  });
}

void TrackDatabase::discardUnsavedMap(int index) {
  if (index < 0 || index >= (int)tracks.size()) return;
  Track& t = tracks[index];
  if (t.mapUnsaved) {
    t.map.clear();
    t.map.shrink_to_fit();
    t.hasMap = false;
    t.mapUnsaved = false;
  }
  if (t.sectorsUnsaved) {
    t.sectors.clear();
    t.sectorsUnsaved = false;
  }
}

bool TrackDatabase::setStartFinish(int index, double lat, double lon, float headingDeg) {
  if (index < 0 || index >= (int)tracks.size()) return false;
  Track& t = tracks[index];
  const double oldRefLat = t.refLat(), oldRefLon = t.refLon();
  t.startFinish = {lat, lon, t.startFinish.radius > 0 ? t.startFinish.radius : settings::defaultGateM(t.kart)};
  if (headingDeg >= 0) {
    t.startFinish.heading = headingDeg;
    t.startFinish.headingFrom = HeadingFrom::Set;
  }
  for (TrackPoint& sp : t.splits)  // guessed to match the old line: guess again below (setLoadedMap)
    if (sp.headingFrom == HeadingFrom::Map) sp.headingFrom = HeadingFrom::None;
  if (!t.sectors.empty()) {  // numbered from the old line: made again from the next clean lap
    t.sectors.clear();
    t.sectorsUnsaved = !t.bench;
  }
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
  if (t.hasMap && t.map.empty()) {  // not loaded: read it, relative to the start/finish now
    loadMap(index);
    return ok;
  }
  // The map is kept relative to the start/finish now: move it there (saves reading tracks.json again)
  geo::XY shift = geo::toLocal(t.refLat(), t.refLon(), oldRefLat, oldRefLon);
  layout::Lines map;
  map.swap(t.map);
  for (std::vector<geo::XYf>& line : map)
    for (geo::XYf& p : line) {
      p.x -= (float)shift.x;
      p.y -= (float)shift.y;
    }
  setLoadedMap(index, std::move(map));
  return ok;
}

void TrackDatabase::setMap(int index, const std::vector<geo::XYf>& line) {
  if (index < 0 || index >= (int)tracks.size() || line.size() < 2) return;
  Track& t = tracks[index];
  t.map.clear();
  t.map.push_back(line);
  t.pitLines.clear();
  t.hasMap = true;
  t.mapUnsaved = !t.bench;
  DEBUG_PRINTF("Tracks: map for '%s' kept (%d points)%s\n", t.displayName().c_str(), (int)line.size(),
               t.mapUnsaved ? ", saved when the session ends" : "");
}

void TrackDatabase::setSectors(int index, const std::vector<TrackPoint>& gates) {
  if (index < 0 || index >= (int)tracks.size()) return;
  Track& t = tracks[index];
  t.sectors = gates;
  t.sectorsUnsaved = !t.bench;
  DEBUG_PRINTF("Tracks: %d corner sectors for '%s' kept%s\n", (int)gates.size() + 1, t.displayName().c_str(),
               t.sectorsUnsaved ? ", saved when the session ends" : "");
}

bool TrackDatabase::saveMaps() {
  size_t points = 0, gates = 0;
  for (const Track& t : tracks) {
    if (t.mapUnsaved)
      for (const std::vector<geo::XYf>& line : t.map) points += line.size();
    if (t.sectorsUnsaved) gates += t.sectors.size() + 1;
  }
  if (!points && !gates) return true;

  // Each point: three slots ([lat,lon] and its two numbers) and two copied ~11-character numbers.
  // Each gate: an object of four members and three copied numbers.
  bool ok = editFile(points * (JSON_ARRAY_SIZE(3) + 24) + gates * (JSON_OBJECT_SIZE(4) + 48) + 1024, [&](JsonArray arr) {
    for (const Track& t : tracks) {
      if (t.sectorsUnsaved) {
        JsonArray s = findOrAddLayout(arr, t).createNestedArray("sectors");  // replaces any there
        for (const TrackPoint& g : t.sectors) writePoint(s.createNestedObject(), g);
      }
      if (!t.mapUnsaved) continue;
      JsonObject o = findOrAddLayout(arr, t);
      JsonArray m = o.createNestedArray("map");  // replaces any existing map
      for (const std::vector<geo::XYf>& line : t.map) {
        JsonArray l = m.createNestedArray();
        for (const geo::XYf& p : line) {
          double lat, lon;
          geo::fromLocal(p.x, p.y, t.refLat(), t.refLon(), lat, lon);
          JsonArray pt = l.createNestedArray();
          pt.add(serialized(String(lat, 6)));
          pt.add(serialized(String(lon, 6)));
        }
      }
    }
    return true;
  });
  for (Track& t : tracks) {
    if (t.mapUnsaved) {
      DEBUG_PRINTF("Tracks: map for '%s' %s\n", t.displayName().c_str(), ok ? "saved" : "NOT saved");
      t.mapUnsaved = !ok;  // not saved: kept in memory, tried again after the next session
    }
    if (t.sectorsUnsaved) {
      DEBUG_PRINTF("Tracks: sectors for '%s' %s\n", t.displayName().c_str(), ok ? "saved" : "NOT saved");
      t.sectorsUnsaved = !ok;
    }
  }
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
  DEBUG_PRINTF("Tracks: no tracks.json found, using %d built-in tracks\n", (int)tracks.size());
}
