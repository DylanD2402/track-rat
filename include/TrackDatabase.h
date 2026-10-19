/**
 * Track Rat - Track Database
 *
 * Loads track definitions from /tracks.json. Search order:
 *   1. SD card        (easy to edit on a PC)
 *   2. LittleFS       (uploaded with `pio run -t uploadfs`)
 *   3. Built-in list  (compiled into the firmware)
 *
 * A track can exist without a start/finish line (from tools/
 * fetch_canada_tracks.py, or created on the device): it is found by its
 * "center", and the line is set on the device the first time it's driven.
 * Changes made on the device (new tracks, start/finish lines, maps) are
 * written back to tracks.json.
 *
 * A venue in tracks.json can have a "layouts" array (Full, Short, Reverse,
 * the kart track...), each with its own start/finish, splits and map. Each
 * layout is a separate Track here, listed next to the venue's other layouts,
 * with an id of "<venue id>.<layout id>" so anything kept per track (maps,
 * best laps) is kept per layout. A venue without "layouts" is one Track.
 */

#ifndef TRACK_DATABASE_H
#define TRACK_DATABASE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <functional>
#include <vector>
#include "GeoMath.h"
#include "LayoutMatch.h"

struct Track {
  String id;       // unique per layout: the venue id, or "<venue id>.<layout id>"
  String venueId;  // "id" of the entry in tracks.json
  String name;     // venue name
  String layoutName;     // empty for a venue without layouts
  int layoutIndex = -1;  // position in the venue's "layouts", -1 = none
  String location;
  String country;
  float length = 0;  // metres
  bool hasStartFinish = false;
  TrackPoint startFinish = {0, 0, 0};
  double centerLat = 0, centerLon = 0;  // used to find the track when there's no start/finish yet
  std::vector<TrackPoint> splits;
  bool hasMap = false;  // tracks.json has a "map" for it
  // Layout for the map screen, metres east/north of refLat()/refLon().
  // Only filled for the track in use (loadMap) and the bench track.
  std::vector<std::vector<geo::XYf>> map;
  bool bench = false;  // the built-in bench-test track: driven by the simulated car

  double refLat() const { return hasStartFinish ? startFinish.lat : centerLat; }
  double refLon() const { return hasStartFinish ? startFinish.lon : centerLon; }
  String displayName() const { return layoutName.length() ? name + " - " + layoutName : name; }
};

class TrackDatabase {
public:
  void begin();

  int count() const { return tracks.size(); }
  const Track* get(int index) const;
  const char* source() const { return sourceName; }

  // Index of the closest track within maxDistanceM (start/finish, or centre
  // if it has none), or -1. The bench track is never matched.
  int findNearest(double lat, double lon, double maxDistanceM, double* distanceOut = nullptr) const;

  // In-memory only (the bench track). Kept last in the list.
  void add(const Track& track) { tracks.push_back(track); }

  // Load the map for this track (frees the others). Lines with no direction
  // get one from the map near them, if it passes close by (HeadingFrom::Map).
  void loadMap(int index);

  // Layouts of the same venue: how many, which one this is (0-based), and
  // the index of the next one (wrapping round; itself if there's only one).
  int layoutCount(int index) const;
  int layoutNumber(int index) const;
  int nextLayout(int index) const;

  // Tracks that could be the one being driven instead of `index`: every
  // other layout or track with a start/finish and a map within maxDistanceM.
  std::vector<int> layoutCandidates(int index, double maxDistanceM) const;
  // Read these tracks' maps from tracks.json, in metres from refLat/refLon.
  bool readMaps(const std::vector<int>& indices, double refLat, double refLon, std::vector<layout::Lines>& out);

  // These change tracks.json (SD if present, else flash) as well as memory.
  // createTrack returns the new index, or -1.
  int createTrack(double centerLat, double centerLon);
  // headingDeg < 0: no direction (crossings either way count).
  bool setStartFinish(int index, double lat, double lon, float headingDeg = -1);
  bool saveMap(int index, const std::vector<geo::XYf>& line);  // line in metres from refLat/refLon

private:
  std::vector<Track> tracks;
  const char* sourceName = "none";
  fs::FS* fileFs = nullptr;  // where tracks.json was loaded from (nullptr = built-in list)

  bool loadFrom(fs::FS& fs, const char* name);
  void loadBuiltIn();
  fs::FS* writableFs();
  bool editFile(size_t extraBytes, std::function<bool(JsonArray tracks)> change);
  int insertIndex() const;  // before the bench track
  bool readFile(std::function<void(JsonArray tracks)> use);  // parse tracks.json, read only
};

#endif  // TRACK_DATABASE_H
