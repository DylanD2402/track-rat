/**
 * Host-side tests for MapStore.h: maps read back exactly as parsing tracks.json gives them.
 *
 *   g++ -std=c++17 -Iinclude test/test_mapstore.cpp -o /tmp/ms && /tmp/ms
 */

#include "MapStore.h"
#include "check.h"

static const double REF_LAT = 44.0123456, REF_LON = -79.9876543;

static void addLine(mapstore::MapStore<>& m, int n, double lat0, double lon0) {
  m.addLine();
  for (int i = 0; i < n; i++) m.addPoint(lat0 + i * 0.000123, lon0 + i * 0.000231);
}

int main() {
  mapstore::MapStore<> m;
  // venue "a" without layouts: a 5-point line, a 1-point line (dropped), a 2-point line
  m.addMap("a", -1);
  addLine(m, 5, 44.01, -79.98);
  addLine(m, 1, 44.5, -79.5);
  addLine(m, 2, 44.02, -79.97);
  // venue "b", layouts 0 and 1
  m.addMap("b", 0);
  addLine(m, 3, 45.0, -80.0);
  m.addMap("b", 1);
  addLine(m, 4, 45.1, -80.1);
  m.addMap("c", -1);  // a map with no lines
  m.addMap("a", -1);  // a repeat: the first one is kept
  addLine(m, 9, 1, 1);
  CHECK(m.maps() == 4, "four maps, got %d", (int)m.maps());
  CHECK(m.points() == 5 + 1 + 2 + 3 + 4, "points %d", (int)m.points());

  layout::Lines out;
  CHECK(m.read("a", -1, REF_LAT, REF_LON, out), "a found");
  CHECK(out.size() == 2, "a: the one-point line is left out, got %d lines", (int)out.size());
  CHECK(out[0].size() == 5 && out[1].size() == 2, "a: line sizes");
  for (int i = 0; i < 5; i++) {
    geo::XY p = geo::toLocal(44.01 + i * 0.000123, -79.98 + i * 0.000231, REF_LAT, REF_LON);
    CHECK(out[0][i].x == (float)p.x && out[0][i].y == (float)p.y, "a point %d identical to the direct conversion", i);
  }
  CHECK(m.read("b", 1, 45.1, -80.1, out) && out.size() == 1 && out[0].size() == 4, "b layout 1");
  CHECK(out[0][0].x == 0.0f && out[0][0].y == 0.0f, "its first point is the reference");
  CHECK(m.read("b", 0, 45.0, -80.0, out) && out.size() == 1 && out[0].size() == 3, "b layout 0");
  CHECK(m.read("c", -1, 0, 0, out) && out.empty(), "c: known, no lines");
  CHECK(!m.read("zzz", -1, 0, 0, out) && out.empty(), "unknown venue");
  CHECK(!m.read("b", -1, 0, 0, out), "b has layouts only");

  m.clear();
  CHECK(m.maps() == 0 && m.points() == 0 && !m.read("a", -1, 0, 0, out), "cleared");
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("test_mapstore: all passed\n");
  return 0;
}
