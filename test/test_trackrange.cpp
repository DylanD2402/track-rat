/**
 * Host-side tests for how far from a track is too far (include/TrackRange.h): a start/finish line far from
 * its track's centre is ignored, and a session can't start on a track further than that from the GPS fix.
 *
 *   g++ -std=c++17 -Iinclude test/test_trackrange.cpp -o /tmp/tr && /tmp/tr
 */

#include <cstdio>
#include "TrackRange.h"
#include "check.h"

using namespace trackrange;

// Wasaga Speedway's real centre, and the line that was set at home near Ottawa by accident
static const double WLAT = 44.4852149, WLON = -80.0437177;
static const double HLAT = 45.2150716, HLON = -75.6728516;

// A latitude this many metres due north of lat
static double northOf(double lat, double metres) { return lat + metres / geo::EARTH_RADIUS_M / geo::DEG2RAD; }

static void testLineBelongs() {
  CHECK(lineBelongs(WLAT, WLON, WLAT, WLON), "a line at the centre belongs");
  CHECK(lineBelongs(WLAT, WLON, northOf(WLAT, 3000), WLON), "3 km out belongs");
  CHECK(lineBelongs(WLAT, WLON, northOf(WLAT, TRACK_RANGE_M - 10), WLON), "just inside the limit belongs");
  CHECK(!lineBelongs(WLAT, WLON, northOf(WLAT, TRACK_RANGE_M + 10), WLON), "just past the limit does not");
  // exactly at the limit: the distance is what decides, and "not further than" is kept
  const double edgeLat = northOf(WLAT, TRACK_RANGE_M);
  CHECK(lineBelongs(WLAT, WLON, edgeLat, WLON) == (geo::distanceM(WLAT, WLON, edgeLat, WLON) <= TRACK_RANGE_M),
        "exactly at the limit follows the distance");
  CHECK(!lineBelongs(WLAT, WLON, HLAT, HLON), "Wasaga's line set at home does not belong");
  CHECK(lineBelongs(0, 0, HLAT, HLON), "a track with no centre can't be judged: belongs");
  CHECK(lineBelongs(0, 0, 0, 0), "nothing at all belongs");
  CHECK(!lineBelongs(0, -80.0, 45.0, -80.0), "latitude 0 with a real longitude is a real centre");
  printf("  lineBelongs: only a real centre with a line over the limit rejects it\n");
}

static void testKm() {
  const double km = kmFrom(WLAT, WLON, HLAT, HLON);
  CHECK(km > 353.0 && km < 355.5, "Wasaga to home is about 354 km (%.2f)", km);
  CHECK((int)(km + 0.5) == 354, "rounds to 354 km (%.2f)", km);
  CHECK(kmFrom(WLAT, WLON, WLAT, WLON) == 0.0, "no distance to itself");
  printf("  kmFrom: %.1f km\n", km);
}

static void testCanStart() {
  CHECK(canStart(true, false, WLAT, WLON, 0, 0) == Start::Ok, "bench with no fix: Ok");
  CHECK(canStart(true, true, WLAT, WLON, HLAT, HLON) == Start::Ok, "bench far away: Ok");
  CHECK(canStart(false, false, WLAT, WLON, HLAT, HLON) == Start::NoFix, "no fix: NoFix");
  CHECK(canStart(false, false, 0, 0, 0, 0) == Start::NoFix, "no fix and no centre: NoFix");
  CHECK(canStart(false, true, WLAT, WLON, HLAT, HLON) == Start::TooFar, "354 km away: TooFar");
  CHECK(canStart(false, true, WLAT, WLON, WLAT, WLON) == Start::Ok, "at the track: Ok");
  CHECK(canStart(false, true, WLAT, WLON, northOf(WLAT, 5000), WLON) == Start::Ok, "5 km away: Ok");
  CHECK(canStart(false, true, WLAT, WLON, northOf(WLAT, TRACK_RANGE_M - 10), WLON) == Start::Ok, "just inside: Ok");
  CHECK(canStart(false, true, WLAT, WLON, northOf(WLAT, TRACK_RANGE_M + 10), WLON) == Start::TooFar, "just past: TooFar");
  CHECK(canStart(false, true, 0, 0, HLAT, HLON) == Start::Ok, "a track with no centre: Ok");
  printf("  canStart: bench exempt, then fix, then distance from the centre\n");
}

int main() {
  printf("Track range tests\n");
  testLineBelongs();
  testKm();
  testCanStart();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nAll track range tests passed\n", failures);
  return failures ? 1 : 0;
}
