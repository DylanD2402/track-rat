/**
 * Track Rat - Track Database Implementation
 */

#include "TrackDatabase.h"
#include "Config.h"
#include "GeoMath.h"
#include "Storage.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SD_MMC.h>

void TrackDatabase::begin() {
  if (Storage::sdMounted() && loadFrom(SD_MMC, "SD")) return;
  if (Storage::flashMounted() && loadFrom(LittleFS, "flash")) return;
  loadBuiltIn();
}

bool TrackDatabase::loadFrom(fs::FS& fs, const char* name) {
  if (!fs.exists(TRACK_DATABASE_FILE)) return false;

  File file = fs.open(TRACK_DATABASE_FILE, "r");
  if (!file) return false;

  DynamicJsonDocument doc(max((size_t)16384, (size_t)file.size() * 2));
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    DEBUG_PRINTF("Tracks: %s%s is invalid JSON: %s\n", name, TRACK_DATABASE_FILE, err.c_str());
    return false;
  }

  tracks.clear();
  for (JsonObject t : doc["tracks"].as<JsonArray>()) {
    Track track;
    track.id = t["id"] | "";
    track.name = t["name"] | "Unnamed";
    track.location = t["location"] | "";
    track.country = t["country"] | "";
    track.length = t["length"] | 0.0f;
    track.startFinish.lat = t["startFinish"]["lat"] | 0.0;
    track.startFinish.lon = t["startFinish"]["lon"] | 0.0;
    track.startFinish.radius = t["startFinish"]["radius"] | DEFAULT_GATE_HALF_WIDTH_M;

    for (JsonObject s : t["splits"].as<JsonArray>()) {
      TrackPoint p;
      p.lat = s["lat"] | 0.0;
      p.lon = s["lon"] | 0.0;
      p.radius = s["radius"] | DEFAULT_GATE_HALF_WIDTH_M;
      track.splits.push_back(p);
    }

    if (track.startFinish.lat == 0.0 && track.startFinish.lon == 0.0) {
      DEBUG_PRINTF("Tracks: skipping '%s' (no start/finish coordinates)\n", track.name.c_str());
      continue;
    }
    tracks.push_back(track);
  }

  if (tracks.empty()) return false;
  sourceName = name;
  DEBUG_PRINTF("Tracks: loaded %d from %s\n", tracks.size(), name);
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
    double d = geo::distanceM(lat, lon, tracks[i].startFinish.lat, tracks[i].startFinish.lon);
    if (d < bestDist) {
      bestDist = d;
      best = i;
    }
  }
  if (distanceOut) *distanceOut = bestDist;
  return best;
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
    tracks.push_back(t);
  }
  sourceName = "built-in";
  DEBUG_PRINTF("Tracks: no tracks.json found, using %d built-in tracks\n", tracks.size());
}
