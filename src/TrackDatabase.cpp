/**
 * Track Rat - Track Database Implementation
 * With SD Card and LittleFS support
 */

#include "TrackDatabase.h"
#include "Config.h"
#include <math.h>
#include <SD.h>
#include <FS.h>

TrackDatabase::TrackDatabase() : currentTrackIndex(-1), initialized(false) {}

bool TrackDatabase::begin() {
  // Try to mount SD card first (if enabled)
#ifdef USE_SD_CARD
  if (SD.begin(SD_CS)) {
    DEBUG_PRINTLN("SD card mounted successfully");

    // Try to load from SD card
    if (loadTracksFromSD()) {
      return true;
    }
  } else {
    DEBUG_PRINTLN("SD card mount failed, falling back to LittleFS");
  }
#endif

  // Fall back to LittleFS
  if (!LittleFS.begin(true)) {
    DEBUG_PRINTLN("Failed to mount LittleFS");
    return false;
  }

  initialized = true;
  DEBUG_PRINTLN("LittleFS mounted successfully");

  // Try to load existing tracks
  if (!loadTracks()) {
    DEBUG_PRINTLN("No existing track database, creating default...");
    return createDefaultDatabase();
  }

  return true;
}

bool TrackDatabase::loadTracksFromSD() {
  if (!SD.exists(TRACK_DATABASE_FILE)) {
    DEBUG_PRINTLN("Track database file does not exist on SD card");
    return false;
  }

  File file = SD.open(TRACK_DATABASE_FILE, FILE_READ);
  if (!file) {
    DEBUG_PRINTLN("Failed to open track database file from SD card");
    return false;
  }

  String jsonData = file.readString();
  file.close();

  bool success = parseTracksFromJson(jsonData);
  if (success) {
    DEBUG_PRINTLN("Loaded tracks from SD card");
  }
  return success;
}

bool TrackDatabase::loadTracks() {
  if (!LittleFS.exists(TRACK_DATABASE_FILE)) {
    DEBUG_PRINTLN("Track database file does not exist");
    return false;
  }

  File file = LittleFS.open(TRACK_DATABASE_FILE, "r");
  if (!file) {
    DEBUG_PRINTLN("Failed to open track database file");
    return false;
  }

  String jsonData = file.readString();
  file.close();

  return parseTracksFromJson(jsonData);
}

bool TrackDatabase::parseTracksFromJson(const String& json) {
  DynamicJsonDocument doc(8192);  // Adjust size as needed

  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    DEBUG_PRINT("deserializeJson() failed: ");
    DEBUG_PRINTLN(error.c_str());
    return false;
  }

  tracks.clear();

  JsonArray tracksArray = doc["tracks"].as<JsonArray>();
  for (JsonObject trackObj : tracksArray) {
    Track track;
    track.id = trackObj["id"].as<String>();
    track.name = trackObj["name"].as<String>();
    track.location = trackObj["location"].as<String>();
    track.country = trackObj["country"].as<String>();
    track.length = trackObj["length"].as<float>();

    // Start/Finish line
    JsonObject startFinish = trackObj["startFinish"];
    track.startFinish.lat = startFinish["lat"].as<double>();
    track.startFinish.lon = startFinish["lon"].as<double>();
    track.startFinish.radius = startFinish["radius"].as<float>();

    // Split points (optional)
    if (trackObj.containsKey("splits")) {
      JsonArray splitsArray = trackObj["splits"].as<JsonArray>();
      for (JsonObject splitObj : splitsArray) {
        TrackPoint split;
        split.lat = splitObj["lat"].as<double>();
        split.lon = splitObj["lon"].as<double>();
        split.radius = splitObj["radius"].as<float>();
        track.splits.push_back(split);
      }
    }

    tracks.push_back(track);
  }

  DEBUG_PRINTF("Loaded %d tracks from database\n", tracks.size());
  return tracks.size() > 0;
}

bool TrackDatabase::saveTracks() {
  DynamicJsonDocument doc(8192);
  JsonArray tracksArray = doc.createNestedArray("tracks");

  for (const Track& track : tracks) {
    JsonObject trackObj = tracksArray.createNestedObject();
    trackObj["id"] = track.id;
    trackObj["name"] = track.name;
    trackObj["location"] = track.location;
    trackObj["country"] = track.country;
    trackObj["length"] = track.length;

    JsonObject startFinish = trackObj.createNestedObject("startFinish");
    startFinish["lat"] = track.startFinish.lat;
    startFinish["lon"] = track.startFinish.lon;
    startFinish["radius"] = track.startFinish.radius;

    if (!track.splits.empty()) {
      JsonArray splitsArray = trackObj.createNestedArray("splits");
      for (const TrackPoint& split : track.splits) {
        JsonObject splitObj = splitsArray.createNestedObject();
        splitObj["lat"] = split.lat;
        splitObj["lon"] = split.lon;
        splitObj["radius"] = split.radius;
      }
    }
  }

  File file = LittleFS.open(TRACK_DATABASE_FILE, "w");
  if (!file) {
    DEBUG_PRINTLN("Failed to open track database for writing");
    return false;
  }

  serializeJson(doc, file);
  file.close();

  DEBUG_PRINTLN("Track database saved successfully");
  return true;
}

int TrackDatabase::getTrackCount() const {
  return tracks.size();
}

Track* TrackDatabase::getTrack(int index) {
  if (index < 0 || index >= tracks.size()) {
    return nullptr;
  }
  return &tracks[index];
}

Track* TrackDatabase::getCurrentTrack() {
  if (currentTrackIndex < 0 || currentTrackIndex >= tracks.size()) {
    return nullptr;
  }
  return &tracks[currentTrackIndex];
}

bool TrackDatabase::setCurrentTrack(int index) {
  if (index < 0 || index >= tracks.size()) {
    return false;
  }
  currentTrackIndex = index;
  DEBUG_PRINTF("Current track set to: %s\n", tracks[index].name.c_str());
  return true;
}

double TrackDatabase::calculateDistance(double lat1, double lon1, double lat2, double lon2) {
  // Haversine formula
  const double R = 6371000.0;  // Earth's radius in meters

  double lat1Rad = lat1 * DEG_TO_RAD;
  double lat2Rad = lat2 * DEG_TO_RAD;
  double deltaLat = (lat2 - lat1) * DEG_TO_RAD;
  double deltaLon = (lon2 - lon1) * DEG_TO_RAD;

  double a = sin(deltaLat / 2.0) * sin(deltaLat / 2.0) +
             cos(lat1Rad) * cos(lat2Rad) *
             sin(deltaLon / 2.0) * sin(deltaLon / 2.0);

  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

  return R * c;
}

int TrackDatabase::findNearestTrack(double lat, double lon) {
  if (tracks.empty()) {
    return -1;
  }

  int nearestIndex = -1;
  double minDistance = TRACK_DETECTION_RADIUS;

  for (int i = 0; i < tracks.size(); i++) {
    double distance = calculateDistance(
      lat, lon,
      tracks[i].startFinish.lat,
      tracks[i].startFinish.lon
    );

    if (distance < minDistance) {
      minDistance = distance;
      nearestIndex = i;
    }
  }

  if (nearestIndex >= 0) {
    DEBUG_PRINTF("Nearest track: %s (%.1f m away)\n",
                 tracks[nearestIndex].name.c_str(), minDistance);
  }

  return nearestIndex;
}

bool TrackDatabase::isNearTrack(double lat, double lon, int trackIndex, float maxDistance) {
  if (trackIndex < 0 || trackIndex >= tracks.size()) {
    return false;
  }

  double distance = calculateDistance(
    lat, lon,
    tracks[trackIndex].startFinish.lat,
    tracks[trackIndex].startFinish.lon
  );

  return distance <= maxDistance;
}

double TrackDatabase::distanceToStartFinish(double lat, double lon) {
  if (currentTrackIndex < 0) {
    return -1.0;
  }
  return distanceToStartFinish(lat, lon, currentTrackIndex);
}

double TrackDatabase::distanceToStartFinish(double lat, double lon, int trackIndex) {
  if (trackIndex < 0 || trackIndex >= tracks.size()) {
    return -1.0;
  }

  return calculateDistance(
    lat, lon,
    tracks[trackIndex].startFinish.lat,
    tracks[trackIndex].startFinish.lon
  );
}

bool TrackDatabase::addTrack(const Track& track) {
  tracks.push_back(track);
  return saveTracks();
}

bool TrackDatabase::removeTrack(int index) {
  if (index < 0 || index >= tracks.size()) {
    return false;
  }

  tracks.erase(tracks.begin() + index);

  if (currentTrackIndex == index) {
    currentTrackIndex = -1;
  } else if (currentTrackIndex > index) {
    currentTrackIndex--;
  }

  return saveTracks();
}

bool TrackDatabase::createDefaultDatabase() {
  tracks.clear();

  // Example Track 1: Laguna Seca
  Track lagunaSeca;
  lagunaSeca.id = "laguna-seca";
  lagunaSeca.name = "Laguna Seca";
  lagunaSeca.location = "Monterey, CA";
  lagunaSeca.country = "USA";
  lagunaSeca.length = 3602.0;
  lagunaSeca.startFinish.lat = 36.584722;
  lagunaSeca.startFinish.lon = -121.753889;
  lagunaSeca.startFinish.radius = 15.0;
  tracks.push_back(lagunaSeca);

  // Example Track 2: Nürburgring GP
  Track nurburgring;
  nurburgring.id = "nurburgring-gp";
  nurburgring.name = "Nürburgring GP";
  nurburgring.location = "Nürburg";
  nurburgring.country = "Germany";
  nurburgring.length = 5148.0;
  nurburgring.startFinish.lat = 50.335278;
  nurburgring.startFinish.lon = 6.947222;
  nurburgring.startFinish.radius = 15.0;
  tracks.push_back(nurburgring);

  // Example Track 3: Silverstone
  Track silverstone;
  silverstone.id = "silverstone";
  silverstone.name = "Silverstone";
  silverstone.location = "Silverstone";
  silverstone.country = "UK";
  silverstone.length = 5891.0;
  silverstone.startFinish.lat = 52.070278;
  silverstone.startFinish.lon = -1.016667;
  silverstone.startFinish.radius = 15.0;
  tracks.push_back(silverstone);

  // Example Track 4: Circuit of the Americas
  Track cota;
  cota.id = "cota";
  cota.name = "Circuit of the Americas";
  cota.location = "Austin, TX";
  cota.country = "USA";
  cota.length = 5513.0;
  cota.startFinish.lat = 30.132778;
  cota.startFinish.lon = -97.641111;
  cota.startFinish.radius = 15.0;
  tracks.push_back(cota);

  // Add your local track here
  Track customTrack;
  customTrack.id = "custom-track";
  customTrack.name = "My Local Track";
  customTrack.location = "Your Location";
  customTrack.country = "Your Country";
  customTrack.length = 2000.0;
  customTrack.startFinish.lat = 0.0;  // SET YOUR COORDINATES
  customTrack.startFinish.lon = 0.0;  // SET YOUR COORDINATES
  customTrack.startFinish.radius = 15.0;
  tracks.push_back(customTrack);

  DEBUG_PRINTF("Created default database with %d tracks\n", tracks.size());
  return saveTracks();
}
