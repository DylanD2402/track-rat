/**
 * Track Rat - Track Database
 *
 * Loads track definitions from /tracks.json. Search order:
 *   1. SD card        (easy to edit on a PC)
 *   2. LittleFS       (uploaded with `pio run -t uploadfs`)
 *   3. Built-in list  (compiled into the firmware)
 */

#ifndef TRACK_DATABASE_H
#define TRACK_DATABASE_H

#include <Arduino.h>
#include <FS.h>
#include <vector>
#include "GeoMath.h"

struct Track {
  String id;
  String name;
  String location;
  String country;
  float length = 0;  // metres
  TrackPoint startFinish = {0, 0, 0};
  std::vector<TrackPoint> splits;
  // Track layout for the map screen: one or more lines, in metres east/north
  // of the start/finish. Empty if tracks.json has no "map" for this track.
  std::vector<std::vector<geo::XYf>> map;
};

class TrackDatabase {
public:
  void begin();

  int count() const { return tracks.size(); }
  const Track* get(int index) const;
  const char* source() const { return sourceName; }

  // Index of the closest track whose start/finish is within maxDistanceM, or -1.
  int findNearest(double lat, double lon, double maxDistanceM, double* distanceOut = nullptr) const;

  void add(const Track& track) { tracks.push_back(track); }

private:
  std::vector<Track> tracks;
  const char* sourceName = "none";

  bool loadFrom(fs::FS& fs, const char* name);
  void loadBuiltIn();
};

#endif  // TRACK_DATABASE_H
