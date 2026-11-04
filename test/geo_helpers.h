// Geometry and driving helpers shared by the lap-timer host tests.
#pragma once
#include <cmath>
#include "GeoMath.h"

// Metres per degree of latitude on a 6371 km sphere.
inline constexpr double M_PER_DEG_LAT = 111194.93;

// Metres per degree of longitude at a latitude.
inline double mPerDegLon(double lat) { return M_PER_DEG_LAT * cos(lat * geo::DEG2RAD); }

// A point on a circle of radius r metres whose easternmost point (angle 0) is at (sfLat, sfLon),
// anticlockwise: the circle the simulated cars drive round.
inline void circlePoint(double sfLat, double sfLon, double r, double angle, double& lat, double& lon) {
  lat = sfLat + r * sin(angle) / M_PER_DEG_LAT;
  lon = sfLon + r * (cos(angle) - 1.0) / (M_PER_DEG_LAT * cos(sfLat * geo::DEG2RAD));
}

// Straight-line driving for the simulated cars in test_adjustline.cpp and test_stage.cpp. Derive as
// `struct Car : CarMotion<Car>`; the car supplies fix(kmh) (one fix, 50 ms later) and public x, y.
template <class Derived>
struct CarMotion {
  double x = 0, y = 0;  // metres east / north of the reference point

  // Stands still for `seconds` (20 fixes a second)
  void wait(double seconds) {
    for (int i = 0; i < seconds * 20; i++) self().fix(0);
  }
  // Straight to (tx, ty): from a standstill (accelerating at `accel` m/s^2) or rolling, at up to vmax km/h
  void driveTo(double tx, double ty, float vmaxKmh, float startKmh = 0, double accel = 6) {
    double v = startKmh / 3.6;
    const double vmax = vmaxKmh / 3.6;
    const double len = hypot(tx - x, ty - y), ux = (tx - x) / len, uy = (ty - y) / len;
    double done = 0;
    while (done < len) {
      v = fmin(vmax, v + accel * 0.05);
      const double step = fmin(v * 0.05, len - done);
      done += step;
      x += ux * step;
      y += uy * step;
      self().fix((float)(v * 3.6));
    }
  }
  // Brakes to a stop over the (dx, dy) metres onwards
  void stopWithin(double dx, double dy, float fromKmh) {
    double v = fromKmh / 3.6;
    const double len = hypot(dx, dy), ux = dx / len, uy = dy / len;
    const double decel = v * v / (2 * len);
    while (v > 0.05) {
      v = fmax(0, v - decel * 0.05);
      x += ux * v * 0.05;
      y += uy * v * 0.05;
      self().fix((float)(v * 3.6));
    }
    self().fix(0);
  }

 private:
  Derived& self() { return *static_cast<Derived*>(this); }
};
