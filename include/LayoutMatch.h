/**
 * Track Rat - Track layout matching
 *
 * Compares a driven path with a layout's map, and works out a line's
 * direction from the map. Pure math, no Arduino dependencies, so it can be
 * unit-tested on a PC (see test/test_laptimer.cpp).
 *
 * Paths and maps are in metres east/north of the same reference point.
 */

#ifndef LAYOUT_MATCH_H
#define LAYOUT_MATCH_H

#include <math.h>
#include <vector>
#include "GeoMath.h"

namespace layout {

using Lines = std::vector<std::vector<geo::XYf>>;

constexpr float MATCH_TOLERANCE_M = 15.0f;  // path within this of the map counts as on it
constexpr float MATCH_STEP_M = 10.0f;       // sample spacing along paths and maps
constexpr float MATCH_MIN_SCORE = 0.75f;    // switch only to a layout scoring at least this...
constexpr float MATCH_MARGIN = 0.15f;       // ...and this much better than the current one

// Squared distance from (x, y) to the nearest segment of `lines`. If `segIdx`
// is given, it gets the line and point index of that segment's start.
inline float nearestD2(const Lines& lines, float x, float y, size_t* lineIdx = nullptr, size_t* segIdx = nullptr) {
  float best = 1e30f;
  for (size_t l = 0; l < lines.size(); l++) {
    const std::vector<geo::XYf>& line = lines[l];
    for (size_t i = 0; i + 1 < line.size(); i++) {
      float ax = line[i].x, ay = line[i].y;
      float dx = line[i + 1].x - ax, dy = line[i + 1].y - ay;
      float len2 = dx * dx + dy * dy;
      float u = len2 > 1e-6f ? ((x - ax) * dx + (y - ay) * dy) / len2 : 0;
      u = u < 0 ? 0 : (u > 1 ? 1 : u);
      float px = ax + u * dx - x, py = ay + u * dy - y;
      float d2 = px * px + py * py;
      if (d2 < best) {
        best = d2;
        if (lineIdx) *lineIdx = l;
        if (segIdx) *segIdx = i;
      }
    }
  }
  return best;
}

// Direction of the map at (x, y), in the order its points are listed, if a
// map line passes within maxDistM. Maps saved from a lap run in the driving
// direction; maps from OpenStreetMap usually do, but not always.
inline bool headingFromMap(const Lines& lines, double x, double y, float maxDistM, float& headingDeg) {
  size_t l = 0, i = 0;
  if (nearestD2(lines, (float)x, (float)y, &l, &i) > maxDistM * maxDistM) return false;
  const std::vector<geo::XYf>& line = lines[l];
  // Widen short segments to about 10 m so one GPS wobble doesn't decide it.
  size_t a = i, b = i + 1;
  double dx = 0, dy = 0;
  for (bool forward = true;; forward = !forward) {
    dx = line[b].x - line[a].x;
    dy = line[b].y - line[a].y;
    if (dx * dx + dy * dy >= 100.0 || (a == 0 && b + 1 == line.size())) break;
    if (forward ? b + 1 < line.size() : a == 0) b++;
    else a--;
  }
  if (dx * dx + dy * dy < 1e-6) return false;
  headingDeg = geo::bearingDeg(dx, dy);
  return true;
}

// Points every `step` metres along a polyline (always including both ends).
inline void resample(const std::vector<geo::XYf>& in, float step, std::vector<geo::XYf>& out) {
  if (in.empty()) return;
  out.push_back(in[0]);
  float carry = 0;
  for (size_t i = 0; i + 1 < in.size(); i++) {
    float dx = in[i + 1].x - in[i].x, dy = in[i + 1].y - in[i].y;
    float len = sqrtf(dx * dx + dy * dy);
    float d = step - carry;
    while (d <= len) {
      out.push_back({in[i].x + dx * d / len, in[i].y + dy * d / len});
      d += step;
    }
    carry = len - (d - step);
  }
  if (in.size() > 1) out.push_back(in.back());
}

struct Score {
  float onMap = 0;     // share of the path that lies on the map
  float coverage = 0;  // share of the map that the path covers
  float value() const { return onMap * coverage; }
};

// How well a driven path fits a layout's map. Both shares matter: driving the
// short layout stays on the full layout's map (onMap 1) but leaves much of it
// unused (low coverage); driving the full layout leaves the short map (low onMap).
inline Score score(const std::vector<geo::XYf>& path, const Lines& map) {
  Score s;
  if (path.size() < 2 || map.empty()) return s;
  const float tol2 = MATCH_TOLERANCE_M * MATCH_TOLERANCE_M;

  std::vector<geo::XYf> pathPts;
  resample(path, MATCH_STEP_M, pathPts);
  Lines pathLine(1, path);
  size_t on = 0;
  for (const geo::XYf& p : pathPts) on += nearestD2(map, p.x, p.y) <= tol2;
  s.onMap = (float)on / pathPts.size();

  size_t covered = 0, total = 0;
  std::vector<geo::XYf> mapPts;
  for (const std::vector<geo::XYf>& line : map) {
    mapPts.clear();
    resample(line, MATCH_STEP_M, mapPts);
    for (const geo::XYf& p : mapPts) {
      covered += nearestD2(pathLine, p.x, p.y) <= tol2;
      total++;
    }
  }
  s.coverage = total ? (float)covered / total : 0;
  return s;
}

// True if the path crosses the line at (gx, gy), the right way if it has a
// direction. A layout whose start/finish the lap never crossed can't be the
// layout that was driven.
inline bool pathCrosses(const std::vector<geo::XYf>& path, double gx, double gy, const TrackPoint& gate) {
  for (size_t i = 0; i + 1 < path.size(); i++) {
    geo::XY a = {path[i].x - gx, path[i].y - gy};
    geo::XY b = {path[i + 1].x - gx, path[i + 1].y - gy};
    double frac;
    if (!geo::crossesGate(a, b, gate.radius, frac)) continue;
    if (gate.headingFrom != HeadingFrom::Set || geo::travelsWith(a, b, gate.heading)) return true;
  }
  return false;
}

}  // namespace layout

#endif  // LAYOUT_MATCH_H
