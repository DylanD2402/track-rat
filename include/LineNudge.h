/**
 * Track Rat - moving a timing line in small steps and re-timing from the fixes (9.3a)
 *
 * In autocross the driver taps to set the start or the finish while driving, so the line
 * lands late or early. The fix is to slide it along the direction of travel in 0.5 m steps
 * (about 20 ms at 90 km/h) and see every run's time change. This is the maths for that, with
 * no Arduino dependencies: tested on a PC in test/test_linenudge.cpp, and mirrored in
 * JavaScript by the Wi-Fi page's track editor (web/index.html, tested by test/test_web.js).
 *
 * Everything is in local metres (x east, y north) from any reference point; the fixes and
 * the line must share it (lineAt() puts a TrackPoint there).
 *
 * - shift(): moves a line along its heading (perpendicular to the line). Negative metres
 *   move it earlier, back towards where the cars come from. A line with no heading has no
 *   "along": the caller gives a fallback, the path's direction at the crossing
 *   (pathHeadingDeg()). With neither, the line doesn't move and shift() says so. The line
 *   still has no heading afterwards: crossings either way keep counting, as before.
 * - A run's fixes are far more than a screen needs, so sliceNear() keeps only those near the
 *   line (plus the fix either side, so the segment into and out of the area is whole). Each
 *   fix says if it follows the one before it in the log (joined); crossings are never found
 *   across a gap in the slice.
 * - crossingTimeMs() finds the crossing the way LapTimer does: geo::crossesGate per segment
 *   (half-open, so a fix on the line counts once), the time interpolated between the two
 *   fixes' times and rounded the same way, only segments of at most MAX_FIX_GAP_MS and
 *   moving (MIN_CROSSING_SPEED_KMH at either end), and, for a line with a heading, only
 *   crossings within 90 degrees of it.
 * - newRunMs(): a run's time from its start and finish crossings, each from its own slice.
 *   A standing start begins when the car moves off (the last fix slower than
 *   STAGE_STILL_KMH), not at the line, so moving the start line leaves the time alone
 *   (standingStartHolds() says when that still applies: as LapTimer, only while the car
 *   stood no more than STAGE_LIGHTS_M behind the line and within STAGE_ARM_M of it. Move the
 *   line well ahead of the car and the run starts at the line, like a start with lights). A
 *   rolling start and every lap start at their crossing; for laps the same line is both
 *   ends, so a moved start/finish changes both.
 */

#ifndef LINE_NUDGE_H
#define LINE_NUDGE_H

#include <math.h>
#include <stdint.h>
#include <vector>
#include "Config.h"
#include "GeoMath.h"

namespace linenudge {

constexpr int64_t NONE = INT64_MIN;  // "no crossing", "no time"
constexpr double NEAR_LINE_M = 60.0;  // the default size of the slice kept near a line
constexpr double STEP_M = 0.5;        // one nudge

struct Fix {
  double x = 0, y = 0;  // metres east and north of the shared reference
  int64_t ms = 0;       // GPS time, continuous (no midnight wrap)
  float kmh = 0;
  bool joined = false;  // follows the fix before it in the log (false for the first of a slice)
};

struct Line {
  double x = 0, y = 0;
  float heading = 0;  // degrees clockwise from north, direction of travel
  bool hasHeading = false;
};

// A TrackPoint in the local frame around (refLat, refLon).
inline Line lineAt(const TrackPoint& p, double refLat, double refLon) {
  geo::XY q = geo::toLocal(p.lat, p.lon, refLat, refLon);
  Line l;
  l.x = q.x;
  l.y = q.y;
  l.heading = p.heading;
  l.hasHeading = p.hasHeading();
  return l;
}

// A fix from a log: lat/lon to local metres.
inline Fix fixAt(double lat, double lon, int64_t ms, float kmh, double refLat, double refLon) {
  geo::XY q = geo::toLocal(lat, lon, refLat, refLon);
  Fix f;
  f.x = q.x;
  f.y = q.y;
  f.ms = ms;
  f.kmh = kmh;
  return f;
}

// How far `metres` along the heading moves the line (east, north). Not NaN: the heading given.
inline bool alongStep(float headingDeg, double metres, double& dx, double& dy) {
  if (isnan(headingDeg)) return false;
  dx = metres * sin(headingDeg * geo::DEG2RAD);
  dy = metres * cos(headingDeg * geo::DEG2RAD);
  return true;
}

// Moves the line `metres` along its heading (negative: earlier). A line with no heading uses
// fallbackDeg (the path's direction there), which may be NAN. False, and the line unmoved, if
// there is no direction at all.
inline bool shift(Line& line, double metres, float fallbackDeg = NAN) {
  double dx, dy;
  if (!alongStep(line.hasHeading ? line.heading : fallbackDeg, metres, dx, dy)) return false;
  line.x += dx;
  line.y += dy;
  return true;
}

// The same for a stored point (lat/lon): for the device's own Adjust screen.
inline bool shiftPoint(TrackPoint& p, double metres, float fallbackDeg = NAN) {
  double dx, dy;
  if (!alongStep(p.hasHeading() ? p.heading : fallbackDeg, metres, dx, dy)) return false;
  geo::fromLocal(dx, dy, p.lat, p.lon, p.lat, p.lon);
  return true;
}

// How far `at` is along the line's heading from the line (negative: behind it, where cars
// come from). 0 for a line with no heading.
inline double alongM(const Line& line, double x, double y) {
  if (!line.hasHeading) return 0;
  const double h = line.heading * geo::DEG2RAD;
  return (x - line.x) * sin(h) + (y - line.y) * cos(h);
}

// The fixes near (cx, cy): within radiusM, and the fix before and after each stretch of
// them. `all` is one run's fixes (or the whole log's) in order.
inline std::vector<Fix> sliceNear(const std::vector<Fix>& all, double cx, double cy, double radiusM = NEAR_LINE_M) {
  std::vector<char> near(all.size(), 0);
  for (size_t i = 0; i < all.size(); i++)
    near[i] = hypot(all[i].x - cx, all[i].y - cy) <= radiusM;
  std::vector<Fix> out;
  long last = -2;  // the original index of the last fix kept
  for (size_t i = 0; i < all.size(); i++) {
    if (!((i > 0 && near[i - 1]) || near[i] || (i + 1 < all.size() && near[i + 1]))) continue;
    Fix f = all[i];
    f.joined = last == (long)i - 1;
    last = (long)i;
    out.push_back(f);
  }
  return out;
}

// Every crossing of the line in the slice, as GPS time (ms), oldest first.
// Pass ignoreHeading to count crossings either way (used to find the path's direction).
inline std::vector<int64_t> crossingsMs(const std::vector<Fix>& s, const Line& line, double halfWidth,
                                        bool ignoreHeading = false) {
  std::vector<int64_t> out;
  for (size_t i = 1; i < s.size(); i++) {
    if (!s[i].joined) continue;
    const int64_t dt = s[i].ms - s[i - 1].ms;
    if (dt <= 0 || dt > MAX_FIX_GAP_MS) continue;
    if (s[i].kmh < MIN_CROSSING_SPEED_KMH && s[i - 1].kmh < MIN_CROSSING_SPEED_KMH) continue;
    geo::XY a = {s[i - 1].x - line.x, s[i - 1].y - line.y};
    geo::XY b = {s[i].x - line.x, s[i].y - line.y};
    double frac;
    if (!geo::crossesGate(a, b, halfWidth, frac)) continue;
    if (!ignoreHeading && line.hasHeading && !geo::travelsWith(a, b, line.heading)) continue;
    out.push_back(s[i - 1].ms + (int64_t)(frac * dt + 0.5));
  }
  return out;
}

// The first crossing after afterMs (default: any), or NONE.
inline int64_t crossingTimeMs(const std::vector<Fix>& s, const Line& line, double halfWidth, int64_t afterMs = NONE) {
  for (int64_t t : crossingsMs(s, line, halfWidth))
    if (afterMs == NONE || t > afterMs) return t;
  return NONE;
}

// The direction the path crosses the line (either way counts), degrees; NAN if it doesn't.
// Use it as shift()'s fallback for a line with no heading.
inline float pathHeadingDeg(const std::vector<Fix>& s, const Line& line, double halfWidth) {
  for (size_t i = 1; i < s.size(); i++) {
    if (!s[i].joined) continue;
    geo::XY a = {s[i - 1].x - line.x, s[i - 1].y - line.y};
    geo::XY b = {s[i].x - line.x, s[i].y - line.y};
    double frac;
    if (geo::crossesGate(a, b, halfWidth, frac)) return geo::bearingDeg(b.x - a.x, b.y - a.y);
  }
  return NAN;
}

// Did a standing start still begin when the car moved off, with the start line where it is
// now? `stood` is the last fix slower than STAGE_STOPPED_KMH before the first movement.
// False: the car stood further than STAGE_ARM_M away, or more than STAGE_LIGHTS_M behind a
// line with a heading, so the run starts at the line instead.
inline bool standingStartHolds(const Fix& stood, const Line& start) {
  if (hypot(stood.x - start.x, stood.y - start.y) > STAGE_ARM_M) return false;
  return !start.hasHeading || alongM(start, stood.x, stood.y) >= -STAGE_LIGHTS_M;
}

// A run's time from its two crossings (NONE if either is missing, or the run is shorter than
// minMs: pass the Min lap time setting, as LapTimer does). standing: the run is a standing
// start (see standingStartHolds), so its start is moveOffMs, the time of the last fix slower
// than STAGE_STILL_KMH, and startMs is not used.
inline int64_t newRunMs(int64_t startMs, int64_t finishMs, bool standing = false, int64_t moveOffMs = NONE,
                        int64_t minMs = 0) {
  const int64_t from = standing ? moveOffMs : startMs;
  if (from == NONE || finishMs == NONE) return NONE;
  const int64_t run = finishMs - from;
  return run > 0 && run >= minMs ? run : NONE;
}

}  // namespace linenudge

#endif  // LINE_NUDGE_H
