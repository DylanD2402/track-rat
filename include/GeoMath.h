/**
 * Track Rat - Geometry helpers
 *
 * Pure math, no Arduino dependencies, so it can be unit-tested on a PC
 * (see test/test_geomath.cpp).
 */

#ifndef GEO_MATH_H
#define GEO_MATH_H

#include <math.h>
#include <stdint.h>

// Where a line's direction of travel came from.
enum class HeadingFrom : uint8_t {
  None,  // no direction: crossings either way count
  Set,   // tracks.json, or the GPS course when the line was tapped
  Map,   // worked out from the order of the map's points near the line (may be backwards)
};

// A timing point on a track: start/finish or split.
// Brace-initialising only {lat, lon, radius} leaves it with no direction.
struct TrackPoint {
  double lat;
  double lon;
  float radius;   // gate half-width in metres
  float heading;  // direction of travel across the line, degrees clockwise from north
  HeadingFrom headingFrom;

  bool hasHeading() const { return headingFrom != HeadingFrom::None; }
};

namespace geo {

constexpr double EARTH_RADIUS_M = 6371000.0;
constexpr double DEG2RAD = M_PI / 180.0;

struct XY {
  double x;  // metres east of reference
  double y;  // metres north of reference
};

struct XYf {  // compact version for stored paths
  float x;
  float y;
};

// Great-circle distance in metres.
inline double distanceM(double lat1, double lon1, double lat2, double lon2) {
  double dLat = (lat2 - lat1) * DEG2RAD;
  double dLon = (lon2 - lon1) * DEG2RAD;
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(lat1 * DEG2RAD) * cos(lat2 * DEG2RAD) * sin(dLon / 2) * sin(dLon / 2);
  return 2.0 * EARTH_RADIUS_M * atan2(sqrt(a), sqrt(1.0 - a));
}

// Equirectangular projection around a reference point. Accurate to well under
// a centimetre over the few hundred metres used for gate detection.
inline XY toLocal(double lat, double lon, double refLat, double refLon) {
  XY p;
  p.x = (lon - refLon) * DEG2RAD * EARTH_RADIUS_M * cos(refLat * DEG2RAD);
  p.y = (lat - refLat) * DEG2RAD * EARTH_RADIUS_M;
  return p;
}

// Inverse of toLocal.
inline void fromLocal(double x, double y, double refLat, double refLon, double& lat, double& lon) {
  lat = refLat + y / EARTH_RADIUS_M / DEG2RAD;
  lon = refLon + x / (EARTH_RADIUS_M * cos(refLat * DEG2RAD)) / DEG2RAD;
}

/**
 * Timing gate test.
 *
 * The gate is a line through the origin, perpendicular to the direction of
 * travel, `halfWidth` metres either side. It counts crossings either way;
 * travelsWith() checks the direction. Returns true if the path segment
 * a->b crosses it, and sets `frac` (0..1) to where along a->b the crossing
 * happened, so the crossing time can be interpolated between GPS fixes.
 *
 * The interval is half-open [0,1) so a fix landing exactly on the gate is
 * counted by exactly one segment.
 */
inline bool crossesGate(XY a, XY b, double halfWidth, double& frac) {
  double dx = b.x - a.x;
  double dy = b.y - a.y;
  double len2 = dx * dx + dy * dy;
  if (len2 < 1e-6) return false;  // not moving

  double u = -(a.x * dx + a.y * dy) / len2;  // projection of origin onto a->b
  if (u < 0.0 || u >= 1.0) return false;

  double cx = a.x + u * dx;
  double cy = a.y + u * dy;
  if (cx * cx + cy * cy > halfWidth * halfWidth) return false;

  frac = u;
  return true;
}

// Compass bearing of the direction dx, dy (metres east, north): 0..360, 0 = north.
inline float bearingDeg(double dx, double dy) {
  double d = atan2(dx, dy) / DEG2RAD;
  return (float)(d < 0 ? d + 360.0 : d);
}

// True if travelling a->b is within 90 degrees of headingDeg (exactly 90 is
// not). Used to ignore crossings the wrong way, such as a straight alongside
// or the pit lane.
inline bool travelsWith(XY a, XY b, float headingDeg) {
  double h = headingDeg * DEG2RAD;
  return (b.x - a.x) * sin(h) + (b.y - a.y) * cos(h) > 1e-6;  // metres along the heading
}

}  // namespace geo

#endif  // GEO_MATH_H
