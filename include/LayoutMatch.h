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
// `lineOut` gets which map line it was.
inline bool headingFromMap(const Lines& lines, double x, double y, float maxDistM, float& headingDeg,
                           size_t* lineOut = nullptr) {
  size_t l = 0, i = 0;
  if (nearestD2(lines, (float)x, (float)y, &l, &i) > maxDistM * maxDistM) return false;
  if (lineOut) *lineOut = l;
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

// Directions for lines that have none, from the map (HeadingFrom::Map).
// Positions are in the map's frame. A split only gets one if it's on the
// same map line as the start/finish, and the map's point order is checked
// against the start/finish: if the start/finish direction was set and the
// map runs the other way there, the split guesses are turned round. If the
// start/finish direction is itself a guess, LapTimer turns them all round
// together if it proves backwards.
inline void guessHeadings(const Lines& map, geo::XY sfAt, TrackPoint& sf, const std::vector<geo::XY>& splitAt,
                          std::vector<TrackPoint>& splits) {
  float sfMap = 0;
  size_t sfLine = 0;
  if (!headingFromMap(map, sfAt.x, sfAt.y, sf.radius, sfMap, &sfLine)) return;  // can't relate the map to the line
  if (!sf.hasHeading()) {
    sf.heading = sfMap;
    sf.headingFrom = HeadingFrom::Map;
  }
  bool reversed = cosf((sf.heading - sfMap) * (float)geo::DEG2RAD) < 0;
  for (size_t i = 0; i < splits.size() && i < splitAt.size(); i++) {
    TrackPoint& sp = splits[i];
    float h;
    size_t line;
    if (sp.hasHeading() || !headingFromMap(map, splitAt[i].x, splitAt[i].y, sp.radius, h, &line)) continue;
    if (line != sfLine) continue;  // another way in the map may run the other way
    sp.heading = reversed ? fmodf(h + 180.0f, 360.0f) : h;
    sp.headingFrom = HeadingFrom::Map;
  }
}

// True if (x, y) is within tol metres of any segment of `lines`. Faster than
// nearestD2 for a yes/no answer: skips segments by their bounding box.
inline bool within(const Lines& lines, float x, float y, float tol) {
  const float tol2 = tol * tol;
  for (const std::vector<geo::XYf>& line : lines) {
    for (size_t i = 0; i + 1 < line.size(); i++) {
      float ax = line[i].x, ay = line[i].y, bx = line[i + 1].x, by = line[i + 1].y;
      if ((x < ax - tol && x < bx - tol) || (x > ax + tol && x > bx + tol) || (y < ay - tol && y < by - tol) ||
          (y > ay + tol && y > by + tol))
        continue;
      float dx = bx - ax, dy = by - ay;
      float len2 = dx * dx + dy * dy;
      float u = len2 > 1e-6f ? ((x - ax) * dx + (y - ay) * dy) / len2 : 0;
      u = u < 0 ? 0 : (u > 1 ? 1 : u);
      float px = ax + u * dx - x, py = ay + u * dy - y;
      if (px * px + py * py <= tol2) return true;
    }
  }
  return false;
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
  std::vector<geo::XYf> pathPts;
  resample(path, MATCH_STEP_M, pathPts);
  Lines pathLine(1, path);
  size_t on = 0;
  for (const geo::XYf& p : pathPts) on += within(map, p.x, p.y, MATCH_TOLERANCE_M);
  s.onMap = (float)on / pathPts.size();

  size_t covered = 0, total = 0;
  std::vector<geo::XYf> mapPts;
  for (const std::vector<geo::XYf>& line : map) {
    mapPts.clear();
    resample(line, MATCH_STEP_M, mapPts);
    for (const geo::XYf& p : mapPts) {
      covered += within(pathLine, p.x, p.y, MATCH_TOLERANCE_M);
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

// Two layouts share a start/finish line: within 5 m, and not set to run
// opposite ways (a Reverse layout drawn on the same line doesn't share it).
// Laps timed at one also count at the other.
inline bool sharesLine(const TrackPoint& a, const TrackPoint& b) {
  bool opposite = a.headingFrom == HeadingFrom::Set && b.headingFrom == HeadingFrom::Set &&
                  cosf((a.heading - b.heading) * (float)geo::DEG2RAD) < 0;
  return !opposite && geo::distanceM(a.lat, a.lon, b.lat, b.lon) < 5.0;
}

// A layout that might be the one being driven (see pickLayout).
struct Candidate {
  geo::XY sfAt;       // its start/finish, in the path's frame
  TrackPoint sf;      // radius and direction of its line
  const Lines* map;   // its map, in the path's frame
};

// Which candidate to switch to, or -1 to stay. A candidate must have its
// start/finish crossed by the path (the right way, if its direction is set),
// score at least MATCH_MIN_SCORE, and beat currentScore by MATCH_MARGIN.
// currentScore is the layout in use: 0 if its line wasn't crossed or it has no map.
inline int pickLayout(const std::vector<geo::XYf>& path, float currentScore, const std::vector<Candidate>& cands,
                      float* bestScoreOut = nullptr) {
  int best = -1;
  float bestScore = 0;
  for (size_t k = 0; k < cands.size(); k++) {
    const Candidate& c = cands[k];
    if (!c.map || !pathCrosses(path, c.sfAt.x, c.sfAt.y, c.sf)) continue;
    float v = score(path, *c.map).value();
    if (v > bestScore) {
      bestScore = v;
      best = (int)k;
    }
  }
  if (bestScoreOut) *bestScoreOut = bestScore;
  if (best < 0 || bestScore < MATCH_MIN_SCORE || bestScore < currentScore + MATCH_MARGIN) return -1;
  return best;
}

}  // namespace layout

#endif  // LAYOUT_MATCH_H
