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
 *
 * A point-to-point track (autocross, hill climb, sprint) has "type": "stage"
 * and a "finish" as well as its "startFinish" (the start): see
 * LapTimer::setStage. An autocross course (Courses.h) is a stage with "kind":
 * "course" and "event": "YYYYMMDD", the day it was made, named "Oct 25 course".
 * Courses are left out of the circuit list; the Autocross hub finds them.
 */

#ifndef TRACK_DATABASE_H
#define TRACK_DATABASE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <functional>
#include <memory>
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
  // Corner sector gates ("sectors" in tracks.json), made from the first clean
  // lap (Sectors.h). Kept apart from the splits: T1, T2... not S1, S2...
  std::vector<TrackPoint> sectors;
  bool sectorsUnsaved = false;  // made on the device (setSectors), not in tracks.json yet
  bool hasMap = false;  // tracks.json has a "map" for it (or will: mapUnsaved)
  // Layout for the map screen, metres east/north of refLat()/refLon().
  // Only filled for the track in use (loadMap), the bench track, and a map
  // not saved yet.
  std::vector<std::vector<geo::XYf>> map;
  bool mapUnsaved = false;  // map made on the device (setMap), not in tracks.json yet
  // Which lines of map are the pit lane ("pit" in tracks.json: indexes into "map",
  // tagged by tools/fetch_track_map.py). In the pit lane the slow screen shows.
  std::vector<uint16_t> pitLines;
  bool bench = false;  // the built-in bench-test track: driven by the simulated car
  // A kart track ("kind": "kart", or every track with the Kart everywhere setting):
  // lines with no "radius" are KART_GATE_HALF_WIDTH_M either side, and laps can be shorter
  bool kart = false;
  // Point to point ("type": "stage"): startFinish is the start, and runs end at the finish
  bool stage = false;
  bool hasFinish = false;
  TrackPoint finish = {0, 0, 0};
  // An autocross course (Courses.h): a stage made for one event day. event is "YYYYMMDD", "" if not known.
  bool course = false;
  String event;

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
  // Courses are left out unless includeCourses (they are found by the Autocross hub, not as circuits).
  int findNearest(double lat, double lon, double maxDistanceM, double* distanceOut = nullptr,
                  bool includeCourses = false) const;

  // In-memory only (the bench track). Kept last in the list.
  void add(const Track& track) { tracks.push_back(track); }

  // Load the map for this track (frees the others). Lines with no direction
  // get one from the map near them, if it passes close by (HeadingFrom::Map).
  void loadMap(int index);
  // The same, with map lines already read (metres from this track's refLat/refLon).
  void setLoadedMap(int index, layout::Lines&& map);
  // Keep, in memory, guessed directions that LapTimer turned round, so the
  // next session doesn't have to find out again.
  void keepGuessedHeadings(int index, const TrackPoint& startFinish, const std::vector<TrackPoint>& splits);

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
  // New course: a stage starting here (no direction: standing starts), made for the day in the stamp
  // ("YYYYMMDD_hhmmss"): "kind": "course", that "event", named "Oct 25 course" ("... 2" for a second
  // that day). The finish comes later.
  int createCourse(double lat, double lon, const String& stamp);
  // Take back a finish just set (the Undo after the finish tap): the course has none again.
  bool clearFinish(int index);
  // Drop a map and corner sectors made on the device and not saved yet (they came from a run that was undone)
  void discardUnsavedMap(int index);
  // A stage's finish line. headingDeg < 0: no direction.
  bool setFinish(int index, double lat, double lon, float headingDeg = -1);
  // headingDeg < 0: no direction (crossings either way count).
  bool setStartFinish(int index, double lat, double lon, float headingDeg = -1);
  // A map made on the device (line in metres from refLat/refLon) is only
  // kept in memory, as saving it takes a while: saveMaps writes it to
  // tracks.json, when nothing is being timed. The bench track's is never saved.
  void setMap(int index, const std::vector<geo::XYf>& line);
  // Corner sector gates, the same way: kept in memory, written by saveMaps.
  void setSectors(int index, const std::vector<TrackPoint>& gates);
  bool saveMaps();  // maps and sectors made on the device

private:
  // Every track's map points, copied out of tracks.json when it is loaded or saved (MapStore.h), so loadMap and
  // readMaps don't parse the whole file. Defined in TrackDatabase.cpp.
  struct MapCache;
  std::shared_ptr<MapCache> mapCache;
  bool mapsFromCache(const Track& t, double refLat, double refLon, layout::Lines& out);
  void rebuildMapCache(JsonArray arr, size_t fileBytes);

  std::vector<Track> tracks;
  const char* sourceName = "none";
  fs::FS* fileFs = nullptr;  // where tracks.json was loaded from (nullptr = built-in list)

  bool loadFrom(fs::FS& fs, const char* name);
  void loadBuiltIn();
  fs::FS* writableFs();
  bool editFile(size_t extraBytes, std::function<bool(JsonArray tracks)> change);
  int insertIndex() const;  // before the bench track
  bool readFile(std::function<void(JsonArray tracks)> use);  // parse tracks.json, read only
  void migrateCourses();  // old "Course 2026-10-20" stages get "kind": "course" and "event" in tracks.json
};

#endif  // TRACK_DATABASE_H
