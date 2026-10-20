/**
 * Track Rat - automatic corner sectors
 *
 * After the first clean lap at a track, the lap is cut into sectors, one
 * per corner or complex: a cut at the fastest point before each braking
 * zone, so each sector holds a braking zone, the corner and the straight
 * after it (a good exit counts for the corner that earned it).
 *
 * - The lap's GPS speed is smoothed over SECTOR_SMOOTH_MS.
 * - A braking zone is a drop of at least SECTOR_BRAKE_DROP_KMH from a peak;
 *   the cut goes at that peak. Lifts and kinks that lose less don't count.
 * - Pieces shorter than SECTOR_MIN_MS are merged: the cut ending a piece
 *   that starts at the line, or starting one that ends there, goes; between
 *   two cuts, the slower of the two goes (it's the lesser braking zone).
 *
 * Sectors are numbered T1, T2... from the line: T1 runs from the line to
 * the first cut, and the last from the last cut to the line. The cuts are
 * saved with the track as direction-aware gates, like splits (main.cpp),
 * and timed by LapTimer separately from the splits (S1, S2...).
 *
 * No Arduino dependencies: tested on a PC in test/test_sectors.cpp.
 */

#ifndef SECTORS_H
#define SECTORS_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <vector>
#include "Config.h"
#include "GeoMath.h"

namespace sectors {

// A point of the lap: metres from the start/finish, ms into the lap, speed.
struct PathPt {
  float x, y;
  int32_t t;
  float speedKmh;
};

// Where a sector ends (a cut): its point on the lap and the direction of travel there.
struct Cut {
  size_t index;      // into the lap's points
  float x, y;
  int32_t t;
  float speedKmh;    // smoothed
  float headingDeg;  // compass, 0 = north
};

// Speed averaged over the points within windowMs/2 either side of each one.
inline std::vector<float> smoothSpeed(const std::vector<PathPt>& lap, int32_t windowMs) {
  std::vector<float> out(lap.size());
  size_t lo = 0, hi = 0;
  double sum = 0;
  for (size_t i = 0; i < lap.size(); i++) {
    while (hi < lap.size() && lap[hi].t <= lap[i].t + windowMs / 2) sum += lap[hi++].speedKmh;
    while (lap[lo].t < lap[i].t - windowMs / 2) sum -= lap[lo++].speedKmh;
    out[i] = (float)(sum / (hi - lo));
  }
  return out;
}

// The direction of travel at lap[i], from about 10 m before to 10 m after.
inline float headingAt(const std::vector<PathPt>& lap, size_t i) {
  size_t a = i, b = i;
  while (a > 0 && hypotf(lap[a].x - lap[i].x, lap[a].y - lap[i].y) < 10.0f) a--;
  while (b + 1 < lap.size() && hypotf(lap[b].x - lap[i].x, lap[b].y - lap[i].y) < 10.0f) b++;
  if (a == b) return 0;
  return geo::bearingDeg(lap[b].x - lap[a].x, lap[b].y - lap[a].y);
}

// The cuts for one lap (its points from the line round to the line), in
// order from the line. Empty for a lap with no braking zones.
inline std::vector<Cut> findCuts(const std::vector<PathPt>& lap, int32_t minPieceMs = SECTOR_MIN_MS,
                                 float minDropKmh = SECTOR_BRAKE_DROP_KMH, int32_t smoothMs = SECTOR_SMOOTH_MS) {
  std::vector<Cut> cuts;
  if (lap.size() < 3) return cuts;
  const std::vector<float> v = smoothSpeed(lap, smoothMs);

  // Peaks followed by a drop of at least minDropKmh (hysteresis, so wobbles don't count)
  std::vector<size_t> peaks;
  bool rising = true;
  size_t ext = 0;  // the highest point while rising, the lowest while falling
  for (size_t i = 1; i < v.size(); i++) {
    if (rising) {
      if (v[i] >= v[ext]) ext = i;  // the last of a flat top (top speed): the cut goes just before braking
      else if (v[i] <= v[ext] - minDropKmh) {
        peaks.push_back(ext);
        rising = false;
        ext = i;
      }
    } else {
      if (v[i] < v[ext]) ext = i;
      else if (v[i] >= v[ext] + minDropKmh) {
        rising = true;
        ext = i;
      }
    }
  }

  // Merge pieces shorter than minPieceMs, shortest first
  const int32_t endT = lap.back().t;
  for (;;) {
    size_t n = peaks.size();
    if (n == 0) break;
    int32_t shortest = INT32_MAX;
    size_t which = 0;  // piece k runs from cut k-1 (or the line) to cut k (or the line)
    for (size_t k = 0; k <= n; k++) {
      int32_t from = k == 0 ? 0 : lap[peaks[k - 1]].t;
      int32_t to = k == n ? endT : lap[peaks[k]].t;
      if (to - from < shortest) {
        shortest = to - from;
        which = k;
      }
    }
    if (shortest >= minPieceMs) break;
    size_t drop;
    if (which == 0) drop = 0;
    else if (which == n) drop = n - 1;
    else drop = v[peaks[which - 1]] < v[peaks[which]] ? which - 1 : which;
    peaks.erase(peaks.begin() + drop);
  }

  for (size_t p : peaks) cuts.push_back({p, lap[p].x, lap[p].y, lap[p].t, v[p], headingAt(lap, p)});
  return cuts;
}

}  // namespace sectors

#endif  // SECTORS_H
