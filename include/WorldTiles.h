/**
 * Track Rat - world track tiles (ROADMAP Phase 10)
 *
 * The world set is too big for tracks.json, so it lives on the SD card in 2 degree tiles:
 * /world/<lat0>/<lon0>.json, lat0 = floor(lat/2)*2 and lon0 = floor(lon/2)*2 (whole numbers, negative allowed:
 * /world/44/-78.json, /world/-34/150.json). Each file is {"tracks": [...]} in tracks.json's own format.
 * This is the pure part: which tiles to read for a position, when to read again, and which world tracks
 * tracks.json hides. No Arduino code, so test/test_worldtiles.cpp checks it on a PC.
 */

#ifndef WORLD_TILES_H
#define WORLD_TILES_H

#include <algorithm>
#include <math.h>
#include <string>
#include <vector>
#include "GeoMath.h"

namespace worldtiles {

constexpr int TILE_DEG = 2;
constexpr double NEAR_EDGE_M = 10000.0;  // within this of a tile's edge the neighbour is wanted too
constexpr double KEEP_EDGE_M = 15000.0;  // ...and kept until we're this far from it, so GPS wander at the edge doesn't reload
constexpr double HIDE_RADIUS_M = 1000.0;  // a world track this close to one of the user's is hidden

struct Tile {
  int lat0 = 0, lon0 = 0;
  bool operator==(const Tile& o) const { return lat0 == o.lat0 && lon0 == o.lon0; }
};

inline int origin(double deg) { return (int)floor(deg / TILE_DEG) * TILE_DEG; }
inline Tile tileFor(double lat, double lon) { return {origin(lat), origin(lon)}; }

// "/world/44/-78.json"
inline std::string tilePath(const Tile& t) { return "/world/" + std::to_string(t.lat0) + "/" + std::to_string(t.lon0) + ".json"; }

// The tile the position is in, then each neighbour whose edge is within edgeM (diagonals too). Longitude wraps
// at 180; there is nothing beyond the poles. Only usable positions give tiles.
inline std::vector<Tile> tilesNear(double lat, double lon, double edgeM = NEAR_EDGE_M) {
  std::vector<Tile> out;
  if (!(lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0)) return out;
  if (lat >= 90.0) lat = 89.999999;  // (a pole sits in the tile below its edge)
  if (lon >= 180.0) lon -= 360.0;
  const Tile home = tileFor(lat, lon);
  out.push_back(home);
  const double mPerDegLat = geo::EARTH_RADIUS_M * geo::DEG2RAD;
  const double mPerDegLon = mPerDegLat * std::max(0.01, cos(lat * geo::DEG2RAD));
  const double south = (lat - home.lat0) * mPerDegLat, north = (home.lat0 + TILE_DEG - lat) * mPerDegLat;
  const double west = (lon - home.lon0) * mPerDegLon, east = (home.lon0 + TILE_DEG - lon) * mPerDegLon;
  int dLat[3] = {0, 0, 0}, dLon[3] = {0, 0, 0};
  int nLat = 1, nLon = 1;  // [0] is "this tile" in each direction
  if (south <= edgeM) dLat[nLat++] = -TILE_DEG;
  if (north <= edgeM) dLat[nLat++] = TILE_DEG;
  if (west <= edgeM) dLon[nLon++] = -TILE_DEG;
  if (east <= edgeM) dLon[nLon++] = TILE_DEG;
  for (int i = 0; i < nLat; i++)
    for (int k = 0; k < nLon; k++) {
      if (i == 0 && k == 0) continue;
      Tile t = {home.lat0 + dLat[i], home.lon0 + dLon[k]};
      if (t.lat0 < -90 || t.lat0 >= 90) continue;
      if (t.lon0 < -180) t.lon0 += 360;
      if (t.lon0 >= 180) t.lon0 -= 360;
      out.push_back(t);
    }
  return out;
}

// Do the tiles held (`loaded`: every tile tried, even ones with no file) still do for this position? Not when a
// wanted tile isn't held, or a held one is now far away: the edge distance for keeping is larger than for wanting.
inline bool needReload(const std::vector<Tile>& loaded, double lat, double lon) {
  const std::vector<Tile> want = tilesNear(lat, lon, NEAR_EDGE_M);
  if (want.empty()) return false;  // no usable position: keep what there is
  for (const Tile& w : want)
    if (std::find(loaded.begin(), loaded.end(), w) == loaded.end()) return true;
  const std::vector<Tile> keep = tilesNear(lat, lon, KEEP_EDGE_M);
  for (const Tile& l : loaded)
    if (std::find(keep.begin(), keep.end(), l) == keep.end()) return true;
  return false;
}

// Somewhere that belongs to one of the user's tracks (its centre, and its start/finish if it has one)
struct Spot {
  double lat, lon;
};

// A world track is hidden when tracks.json holds the same id, or a track of the user's has a centre or
// start/finish within HIDE_RADIUS_M of the world track's centre.
inline bool hidden(const std::string& id, double lat, double lon, const std::vector<std::string>& ownIds,
                   const std::vector<Spot>& ownSpots, double radiusM = HIDE_RADIUS_M) {
  if (std::find(ownIds.begin(), ownIds.end(), id) != ownIds.end()) return true;
  for (const Spot& s : ownSpots)
    if (geo::distanceM(lat, lon, s.lat, s.lon) <= radiusM) return true;
  return false;
}

}  // namespace worldtiles

#endif  // WORLD_TILES_H
