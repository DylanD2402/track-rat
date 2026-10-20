/**
 * Track Rat - best laps on the card (implementation)
 */

#include "BestLapStore.h"
#include <ArduinoJson.h>
#include "Config.h"
#include "JsonFile.h"
#include "Storage.h"

namespace BestLapStore {

static const char* DIR = "/tracks";

static String pathFor(const String& trackId, Kind kind) {
  return String(DIR) + "/" + trackId + (kind == Kind::Ever ? "/best.bin" : "/day.bin");
}

static bool readFile(fs::FS& fs, const String& path, bestlap::Record& out, bool withPath) {
  File f = fs.open(path, "r");
  if (!f) return false;
  uint8_t head[bestlap::HEADER_BYTES];
  bool ok = false;
  if (f.read(head, sizeof(head)) == (int)sizeof(head)) {
    long n = bestlap::pointCount(head, sizeof(head));
    if (n >= 0 && !withPath) {
      ok = bestlap::decode(head, sizeof(head), out, false);
    } else if (n >= 0) {
      std::vector<uint8_t> data(bestlap::HEADER_BYTES + (size_t)n * bestlap::POINT_BYTES);
      memcpy(data.data(), head, sizeof(head));
      size_t want = data.size() - sizeof(head);
      ok = f.read(data.data() + sizeof(head), want) == want && bestlap::decode(data.data(), data.size(), out, true);
    }
  }
  f.close();
  return ok;
}

bool load(const String& trackId, Kind kind, bestlap::Record& out, bool withPath) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || trackId.length() == 0) return false;
  const String path = pathFor(trackId, kind);
  if (!fs->exists(path)) {
    // A save caught between writing the new copy and renaming it: finish it if the copy is whole
    const String tmp = path + ".tmp";
    bestlap::Record r;
    if (!fs->exists(tmp) || !readFile(*fs, tmp, r, true) || !fs->rename(tmp, path)) return false;
  }
  return readFile(*fs, path, out, withPath);
}

bool save(const String& trackId, Kind kind, const bestlap::Record& r) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || trackId.length() == 0 || !r.valid()) return false;
  const String dir = String(DIR) + "/" + trackId;
  if (!fs->exists(DIR)) fs->mkdir(DIR);
  if (!fs->exists(dir)) fs->mkdir(dir);

  const String path = pathFor(trackId, kind);
  const String tmp = path + ".tmp";
  const std::vector<uint8_t> data = bestlap::encode(r);
  File f = fs->open(tmp, "w");
  if (!f) return false;
  const size_t written = f.write(data.data(), data.size());
  f.close();
  if (written != data.size()) {
    fs->remove(tmp);
    return false;
  }
  fs->remove(path);  // FAT won't rename onto an existing file
  if (!fs->rename(tmp, path)) return false;
  DEBUG_PRINTF("Best lap: %s %s saved to %s:%s (%u points)\n", trackId.c_str(),
               kind == Kind::Ever ? "PB" : "best of the day", Storage::sessionFSName(), path.c_str(),
               (unsigned)r.path.size());
  return true;
}

static String statsPath(const String& trackId) { return String(DIR) + "/" + trackId + "/stats.json"; }

bool loadStats(const String& trackId, Stats& out) {
  out = Stats();
  fs::FS* fs = Storage::sessionFS();
  if (!fs || trackId.length() == 0) return false;
  const String path = statsPath(trackId);
  if (!fs->exists(path)) {  // a save caught mid-rename: finish it (the copy is only renamed once whole)
    if (!fs->exists(path + ".tmp") || !fs->rename(path + ".tmp", path)) return false;
  }
  File f = fs->open(path, "r");
  if (!f) return false;
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  for (JsonArray g : doc["sectorGates"].as<JsonArray>()) out.sectorGates.push_back({g[0] | 0.0, g[1] | 0.0});
  for (JsonVariant v : doc["sectorBestMs"].as<JsonArray>()) out.sectorBestMs.push_back(v | -1);
  out.lastSession = doc["lastSession"]["session"] | 0;
  out.lastDate = doc["lastSession"]["date"] | 0;
  out.lastBestMs = doc["lastSession"]["bestMs"] | 0;
  return true;
}

bool saveStats(const String& trackId, const Stats& s) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || trackId.length() == 0) return false;
  const String dir = String(DIR) + "/" + trackId;
  if (!fs->exists(DIR)) fs->mkdir(DIR);
  if (!fs->exists(dir)) fs->mkdir(dir);
  DynamicJsonDocument doc(1024 + s.sectorGates.size() * 96 + s.sectorBestMs.size() * 16);
  JsonArray gates = doc.createNestedArray("sectorGates");
  for (const geo::XY& g : s.sectorGates) {
    JsonArray p = gates.createNestedArray();
    p.add(serialized(String(g.x, 7)));
    p.add(serialized(String(g.y, 7)));
  }
  JsonArray best = doc.createNestedArray("sectorBestMs");
  for (int32_t v : s.sectorBestMs) best.add(v);
  if (s.lastSession > 0 || s.lastBestMs > 0) {
    doc["lastSession"]["session"] = s.lastSession;
    doc["lastSession"]["date"] = s.lastDate;
    doc["lastSession"]["bestMs"] = s.lastBestMs;
  }
  return writeJsonFile(*fs, statsPath(trackId), doc, true) > 0;
}

}  // namespace BestLapStore
