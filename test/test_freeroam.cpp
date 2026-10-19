/**
 * Host-side tests for Free Roam.
 *
 *   g++ -std=c++17 -Iinclude test/test_freeroam.cpp -o /tmp/fr && /tmp/fr
 */

#include <cmath>
#include <cstdio>
#include "FreeRoam.h"

static int failures = 0;
#define CHECK(cond, ...)                         \
  do {                                           \
    if (!(cond)) {                               \
      failures++;                                \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                       \
      printf("\n");                              \
    }                                            \
  } while (0)

int main() {
  printf("FreeRoam tests\n");
  const double lat0 = 45.0, lon0 = -76.0, mPerDegLat = 111194.93;

  // 2 km due north at 72 km/h (20 m/s), 10 Hz, then parked with 2 m jitter.
  FreeRoam fr;
  fr.start(0);
  for (int i = 0; i <= 1000; i++) fr.onFix(lat0 + (i * 2.0) / mPerDegLat, lon0, 72.0f);
  for (int i = 0; i < 300; i++) fr.onFix(lat0 + 2000.0 / mPerDegLat + ((i % 2) ? 2.0 : -2.0) / mPerDegLat, lon0, 1.0f);

  CHECK(fabs(fr.distanceKm() - 2.0) < 0.002, "distance %.4f km", fr.distanceKm());
  CHECK(fabs(fr.topSpeedKmh() - 72.0f) < 0.01, "top %.1f", fr.topSpeedKmh());
  CHECK(fabs(fr.avgMovingKmh() - 72.0f) < 0.01, "avg %.1f", fr.avgMovingKmh());
  // Fixes are 2 m apart and points need >= 3 m spacing, so every 2nd fix is kept: 4 m apart.
  CHECK(fr.path.size() == 501, "path points %zu (expect 501: one per 4 m)", fr.path.size());
  CHECK(fabs(fr.path.back().y - 2000.0f) < 4.0f, "path end %.1f", fr.path.back().y);
  printf("  2 km drive + parked jitter: %.3f km, top %.0f km/h, %zu path points\n", fr.distanceKm(),
         fr.topSpeedKmh(), fr.path.size());

  // A 5 km GPS glitch must not add distance
  FreeRoam g;
  g.start(0);
  g.onFix(lat0, lon0, 50);
  g.onFix(lat0 + 5000.0 / mPerDegLat, lon0, 50);
  g.onFix(lat0, lon0, 50);
  CHECK(g.distanceKm() < 0.001, "glitch added %.3f km", g.distanceKm());
  printf("  position glitch ignored: %s\n", g.distanceKm() < 0.001 ? "yes" : "no");

  // Very long drives get thinned rather than growing without limit
  FreeRoam l;
  l.start(0);
  for (int i = 0; i < 200000; i++) l.onFix(lat0 + (i * 3.5) / mPerDegLat, lon0, 100);
  CHECK(l.path.size() <= FreeRoam::MAX_POINTS, "path %zu", l.path.size());
  printf("  700 km drive: %zu path points (cap %zu)\n", l.path.size(), FreeRoam::MAX_POINTS);

  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
