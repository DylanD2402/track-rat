/**
 * Track Rat - Free Roam drive log
 *
 * Records the path driven (for the map), distance, top speed and average
 * moving speed. Pure C++; tested in test/test_freeroam.cpp.
 */

#ifndef FREE_ROAM_H
#define FREE_ROAM_H

#include <stdint.h>
#include <vector>
#include "GeoMath.h"

class FreeRoam {
public:
  static constexpr float MIN_POINT_SPACING_M = 3.0f;   // path resolution for the map
  static constexpr float MIN_MOVING_KMH = 3.0f;        // below this, GPS wander isn't distance
  static constexpr double MAX_JUMP_M = 200.0;          // ignore position jumps (GPS glitches)
  static constexpr size_t MAX_POINTS = 60000;          // ~180 km at 3 m spacing

  void start(uint32_t nowMillis) {
    *this = FreeRoam();
    startMillis = nowMillis;
    path.reserve(4096);  // large: allocated in PSRAM on the ESP32
  }

  void onFix(double lat, double lon, float speedKmh) {
    if (!haveRef) {
      refLat = lat;
      refLon = lon;
      haveRef = true;
    }
    geo::XY p = geo::toLocal(lat, lon, refLat, refLon);

    if (haveLast) {
      double d = hypot(p.x - lastX, p.y - lastY);
      if (speedKmh >= MIN_MOVING_KMH && d < MAX_JUMP_M) distanceM += d;
    }
    lastX = p.x;
    lastY = p.y;
    haveLast = true;

    if (speedKmh > maxKmh) maxKmh = speedKmh;
    if (speedKmh >= MIN_MOVING_KMH) {
      movingSpeedSum += speedKmh;
      movingFixes++;
    }

    if (path.empty() || hypot(p.x - path.back().x, p.y - path.back().y) >= MIN_POINT_SPACING_M) {
      if (path.size() >= MAX_POINTS) thin();
      path.push_back({(float)p.x, (float)p.y});
    }
  }

  uint32_t elapsedMs(uint32_t nowMillis) const { return nowMillis - startMillis; }
  double distanceKm() const { return distanceM / 1000.0; }
  float topSpeedKmh() const { return maxKmh; }
  float avgMovingKmh() const { return movingFixes ? (float)(movingSpeedSum / movingFixes) : 0.0f; }
  bool hasPosition() const { return haveRef; }

  double refLat = 0, refLon = 0;     // path is in metres east/north of this
  std::vector<geo::XYf> path;

private:
  bool haveRef = false, haveLast = false;
  double lastX = 0, lastY = 0;
  double distanceM = 0;
  float maxKmh = 0;
  double movingSpeedSum = 0;
  uint32_t movingFixes = 0;
  uint32_t startMillis = 0;

  void thin() {  // keep every other point so very long drives still fit
    size_t j = 0;
    for (size_t i = 0; i < path.size(); i += 2) path[j++] = path[i];
    path.resize(j);
  }
};

#endif  // FREE_ROAM_H
