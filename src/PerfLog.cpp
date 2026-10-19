/**
 * Track Rat - Performance run log implementation
 */

#include "PerfLog.h"
#include "Config.h"
#include "Storage.h"
#include <ArduinoJson.h>

using perf::PerfResult;

namespace PerfLog {

String newPath(const String& stamp) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs) return String();
  if (!fs->exists(SESSION_DIR)) fs->mkdir(SESSION_DIR);
  String base = String(SESSION_DIR) + "/" + (stamp.length() ? stamp : String("run")) + "_perf";
  String path = base + ".json";
  for (int n = 2; fs->exists(path) && n < 1000; n++) path = base + "_" + n + ".json";
  return path;
}

// Adds only the measurements that were reached.
static void addMeasurements(JsonObject o, const PerfResult& r) {
  auto sec = [&](const char* key, float v) {
    if (v >= 0) o[key] = serialized(String(v, 3));
  };
  auto kmh = [&](const char* key, float v) {
    if (v >= 0) o[key] = serialized(String(v, 1));
  };
  sec("t0to60kmh", r.t0to60);
  sec("t0to100kmh", r.t0to100);
  sec("t0to60mph", r.t0to60mph);
  sec("t100to200kmh", r.t100to200);
  sec("eighthMileS", r.et8);
  kmh("eighthMileTrapKmh", r.trap8Kmh);
  sec("quarterMileS", r.et4);
  kmh("quarterMileTrapKmh", r.trap4Kmh);
  if (r.brakeM >= 0) {
    o["brake100to0M"] = serialized(String(r.brakeM, 1));
    sec("brake100to0S", r.brakeS);
  }
}

bool save(const String& path, const String& stamp, const perf::PerfRun& runs) {
  fs::FS* fs = Storage::sessionFS();
  if (!fs || path.length() == 0) return false;

  const std::vector<PerfResult>& results = runs.results();
  DynamicJsonDocument doc(2048 + results.size() * 1024);
  doc["type"] = "performance";
  doc["started"] = stamp;
  doc["runCount"] = results.size();

  JsonArray arr = doc.createNestedArray("runs");
  int n = 0;
  for (const PerfResult& r : results) {
    JsonObject o = arr.createNestedObject();
    o["run"] = ++n;
    o["kind"] = r.kind == perf::RunKind::Launch ? "launch" : r.kind == perf::RunKind::Roll ? "roll" : "brake";
    o["valid"] = r.valid;
    if (!r.valid) o["problem"] = "GPS signal gap";
    if (r.kind == perf::RunKind::Launch) o["rollout"] = r.rollout;
    o["notLevel"] = r.notLevel;
    o["heightChangeM"] = serialized(String(r.climbM, 1));
    o["gradePct"] = serialized(String(r.gradePct, 1));
    o["peakKmh"] = serialized(String(r.peakKmh, 1));
    o["gpsTimeMs"] = r.startGpsMs;
    addMeasurements(o, r);
  }
  addMeasurements(doc.createNestedObject("best"), runs.best());

  File f = fs->open(path, "w");
  if (!f) {
    DEBUG_PRINTF("Perf: could not open %s for writing\n", path.c_str());
    return false;
  }
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

}  // namespace PerfLog
