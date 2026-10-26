// Host test for include/WorldTiles.h: which world tiles to read, when to read again, which tracks are hidden.
//   g++ -std=c++17 -Iinclude test/test_worldtiles.cpp -o /tmp/wt && /tmp/wt
// Reads the small tile set in test/fixtures/world (run from the repository root).

#include <fstream>
#include <sstream>
#include "WorldTiles.h"
#include "check.h"

using namespace worldtiles;

static std::string readAll(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

// The id of every entry in a tile file (the fixtures write each entry as {"id": "..."...)
static std::vector<std::string> idsIn(const std::string& text) {
  std::vector<std::string> ids;
  size_t p = 0;
  while ((p = text.find("{\"id\": \"", p)) != std::string::npos) {
    p += 8;
    ids.push_back(text.substr(p, text.find('"', p) - p));
  }
  return ids;
}

static bool has(const std::vector<Tile>& v, int lat0, int lon0) { return std::find(v.begin(), v.end(), Tile{lat0, lon0}) != v.end(); }

int main() {
  printf("World tile tests\n");

  // The tile key: floor to even degrees, negative numbers round down
  CHECK(tileFor(44.05, -77.0) == (Tile{44, -78}), "44.05,-77 is tile 44/-78");
  CHECK(tileFor(44.0, -78.0) == (Tile{44, -78}), "an exact corner belongs to the tile it starts");
  CHECK(tileFor(-33.8, 151.2) == (Tile{-34, 150}), "Sydney is -34/150");
  CHECK(tileFor(-0.5, -0.5) == (Tile{-2, -2}), "just below zero is -2/-2");
  CHECK(tileFor(0.5, 1.5) == (Tile{0, 0}), "0.5,1.5 is 0/0");
  CHECK(tilePath({44, -78}) == "/world/44/-78.json", "path %s", tilePath({44, -78}).c_str());
  CHECK(tilePath({-34, 150}) == "/world/-34/150.json", "path %s", tilePath({-34, 150}).c_str());

  // Mid-tile (44/-78 spans lat 44..46, lon -78..-76): just the one, and it comes first
  std::vector<Tile> t = tilesNear(45.0, -77.0);
  CHECK(t.size() == 1 && t[0] == (Tile{44, -78}), "middle of a tile: only that tile (%d)", (int)t.size());
  // 0.05 degree of longitude (3.9 km at 45 N) from the west edge
  t = tilesNear(45.0, -77.95);
  CHECK(t.size() == 2 && has(t, 44, -80), "near the west edge: the western neighbour too (%d)", (int)t.size());
  // 0.08 degree (8.9 km) from the north edge
  t = tilesNear(45.92, -77.0);
  CHECK(t.size() == 2 && has(t, 46, -78), "near the north edge: the northern neighbour too (%d)", (int)t.size());
  // 0.1 degree (11.1 km) from the north edge: no neighbour
  t = tilesNear(45.9, -77.0);
  CHECK(t.size() == 1, "11 km from the edge: no neighbour (%d)", (int)t.size());
  // A corner: three neighbours
  t = tilesNear(45.97, -76.03);
  CHECK(t.size() == 4 && has(t, 46, -78) && has(t, 44, -76) && has(t, 46, -76), "a corner: four tiles (%d)", (int)t.size());
  // Longitude gets shorter toward the poles: at 80 N 0.25 degree of longitude is under 5 km
  t = tilesNear(80.5, -77.75);
  CHECK(has(t, 80, -80), "high latitude: the edge is nearer than it looks in degrees (%d)", (int)t.size());
  // Wrap at the date line, and the poles
  t = tilesNear(10.5, 179.95);
  CHECK(has(t, 10, -180), "east of 179.9 wraps to -180 (%d)", (int)t.size());
  t = tilesNear(10.5, -179.95);
  CHECK(has(t, 10, 178), "west of -179.9 wraps to 178");
  t = tilesNear(-89.99, 10.0);
  CHECK(t.size() >= 1 && !has(t, -92, 10), "nothing below the south pole");
  t = tilesNear(89.99, 10.0);
  CHECK(!has(t, 90, 10), "nothing above the north pole");
  CHECK(tilesNear(95.0, 0.0).empty() && tilesNear(0.0, 200.0).empty(), "a nonsense position gives no tiles");

  // Reading again: not while inside what is held, only when a wanted tile is missing or one is far away
  std::vector<Tile> held = tilesNear(45.0, -77.0);
  CHECK(!needReload(held, 45.0, -77.0), "same place: no reload");
  CHECK(!needReload(held, 45.5, -76.5), "moved inside the tile: no reload");
  CHECK(needReload(held, 45.0, -77.95), "reached the western edge: reload for the neighbour");
  held = tilesNear(45.0, -77.95);  // holds 44/-78 and 44/-80
  CHECK(!needReload(held, 45.0, -77.95), "neighbour held: no reload");
  CHECK(!needReload(held, 45.0, -77.88), "7 km further in (still within 15 km of the edge): neighbour kept");
  CHECK(needReload(held, 45.0, -77.5), "40 km in: the neighbour is let go");
  CHECK(needReload(held, 47.0, -77.0), "another tile: reload");
  CHECK(!needReload(held, 95.0, 0.0), "no usable position: nothing to do");
  CHECK(needReload({}, 45.0, -77.0), "nothing held yet: load");

  // Merge: the same id hides it; a track of the user's within 1 km hides it; further away it shows
  std::vector<std::string> ownIds = {"osm-w1003", "my-track-1"};
  std::vector<Spot> own = {{44.2005, -76.5000}, {40.0, -75.0}};
  CHECK(hidden("osm-w1003", 10.0, 10.0, ownIds, own), "same id is hidden wherever it is");
  CHECK(hidden("osm-w1002", 44.2000, -76.5000, ownIds, own), "55 m from one of mine is hidden");
  CHECK(!hidden("osm-w1001", 44.0500, -77.0000, ownIds, own), "far from mine shows");
  CHECK(hidden("x", 44.2005 + 0.0085, -76.5000, ownIds, own), "945 m away is hidden");
  CHECK(!hidden("x", 44.2005 + 0.0095, -76.5000, ownIds, own), "1.06 km away shows");
  CHECK(!hidden("x", 44.0, -78.0, {}, {}), "no tracks of mine: everything shows");

  // The fixture set: the tiles for a few places exist, and hold what the app expects
  const std::string root = "test/fixtures";
  std::string home = readAll(root + tilePath(tileFor(44.05, -77.0)));
  CHECK(!home.empty(), "fixture tile %s exists", tilePath(tileFor(44.05, -77.0)).c_str());
  std::vector<std::string> ids = idsIn(home);
  CHECK(ids.size() == 3, "three tracks in the home tile (%d)", (int)ids.size());
  CHECK(home.find("\"estimated\": true") != std::string::npos, "the home tile has an estimated start/finish");
  CHECK(idsIn(readAll(root + tilePath(tileFor(-33.8, 151.2)))).size() == 1, "the Sydney tile has one track");
  CHECK(readAll(root + tilePath(tileFor(10.0, 10.0))).empty(), "no file for a tile that isn't on the card");
  t = tilesNear(44.5, -77.95);
  CHECK(has(t, 44, -80) && idsIn(readAll(root + tilePath({44, -80}))).size() == 1, "the neighbour tile file is there");
  // Merge on the fixture: with osm-w1003 and a track 55 m from osm-w1002 in my tracks.json, only osm-w1001 shows
  int shown = 0;
  for (const std::string& id : ids) {
    const bool near1002 = id == "osm-w1002";
    if (!hidden(id, near1002 ? 44.2000 : 44.05, near1002 ? -76.5000 : -77.0, ownIds, own)) shown++;
  }
  CHECK(shown == 1, "one fixture track left after merging (%d)", shown);

  // Nearest-N: the flat-earth distance agrees with haversine, and the farthest is the one to drop
  const double hav = geo::distanceM(45.0, -77.0, 45.5, -76.2), apx = approxDistM(45.0, -77.0, 45.5, -76.2);
  CHECK(fabs(hav - apx) / hav < 0.003, "approx %.0f m vs haversine %.0f m", apx, hav);
  CHECK(approxDistM(10.0, 179.9, 10.0, -179.9) < 25000.0, "across the date line is near");
  CHECK(approxDistM(45.0, -77.0, 45.0, -77.0) == 0.0, "same place");
  CHECK(farthest({}) == -1, "none: -1");
  CHECK(farthest({5.0, 90.0, 12.0}) == 1, "the farthest of three");
  {  // 300 tracks on a line, keep the nearest 200 the way refreshWorld does
    std::vector<double> d;
    for (int i = 0; i < 300; i++) {
      d.push_back(approxDistM(45.0, -77.0, 45.0 + ((i * 37) % 300) * 0.001, -77.0));
      while (d.size() > 200) d.erase(d.begin() + farthest(d));
    }
    double worst = 0;
    for (double x : d) worst = std::max(worst, x);
    CHECK(d.size() == 200 && worst < 0.2 * 111195.0, "200 kept, all within 22 km (%zu, %.0f)", d.size(), worst);
  }

  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("All passed\n");
  return 0;
}
