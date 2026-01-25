/**
 * Track Rat - Track Database Manager
 * Handles local track database with coordinates
 */

#ifndef TRACK_DATABASE_H
#define TRACK_DATABASE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <vector>

struct TrackPoint {
  double lat;
  double lon;
  float radius;  // detection radius in meters
};

struct Track {
  String id;
  String name;
  String location;
  String country;
  float length;  // meters
  TrackPoint startFinish;
  std::vector<TrackPoint> splits;  // optional split points

  Track() : length(0.0) {}
};

class TrackDatabase {
private:
  std::vector<Track> tracks;
  int currentTrackIndex;
  bool initialized;

  double calculateDistance(double lat1, double lon1, double lat2, double lon2);
  bool parseTracksFromJson(const String& json);

public:
  TrackDatabase();

  bool begin();
  bool loadTracks();
  bool saveTracks();

  // Track management
  int getTrackCount() const;
  Track* getTrack(int index);
  Track* getCurrentTrack();
  bool setCurrentTrack(int index);

  // Track detection
  int findNearestTrack(double lat, double lon);
  bool isNearTrack(double lat, double lon, int trackIndex, float maxDistance);

  // Track operations
  bool addTrack(const Track& track);
  bool removeTrack(int index);

  // Distance calculations
  double distanceToStartFinish(double lat, double lon);
  double distanceToStartFinish(double lat, double lon, int trackIndex);

  // Create default database
  bool createDefaultDatabase();
};

#endif // TRACK_DATABASE_H
