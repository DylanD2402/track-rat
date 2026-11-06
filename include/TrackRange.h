/**
 * Track Rat - how far from a track is too far
 *
 * A session on a track, or a start/finish line for it, only makes sense near the track. A line set far
 * away (at home, by accident) made the track "2 m away" at every start, because the nearest-track search
 * and the maps use the line when there is one. So:
 *   - a line more than TRACK_RANGE_M from the track's centre doesn't belong to it and is ignored;
 *   - a session can't start on a track whose centre is more than TRACK_RANGE_M from the GPS fix.
 * A track with no centre (0, 0) can't be judged and is always fine. The bench track (simulated car) is exempt.
 *
 * Pure logic, no Arduino code, so it is tested on a PC (test/test_trackrange.cpp).
 */

#ifndef TRACK_RANGE_H
#define TRACK_RANGE_H

#include "Config.h"
#include "GeoMath.h"

namespace trackrange {

inline bool hasCenter(double lat, double lon) { return lat != 0.0 || lon != 0.0; }

// Kilometres between two places.
inline double kmFrom(double lat1, double lon1, double lat2, double lon2) {
  return geo::distanceM(lat1, lon1, lat2, lon2) / 1000.0;
}

// False only when the track has a real centre and the line is further than TRACK_RANGE_M from it.
inline bool lineBelongs(double centerLat, double centerLon, double lineLat, double lineLon) {
  if (!hasCenter(centerLat, centerLon)) return true;
  return geo::distanceM(centerLat, centerLon, lineLat, lineLon) <= TRACK_RANGE_M;
}

enum class Start { Ok, NoFix, TooFar };

// Can a session start on this track from here? The bench is always fine; otherwise a fix is needed, and a
// track with a centre must be within TRACK_RANGE_M of it.
inline Start canStart(bool bench, bool hasFix, double centerLat, double centerLon, double fixLat, double fixLon) {
  if (bench) return Start::Ok;
  if (!hasFix) return Start::NoFix;
  if (hasCenter(centerLat, centerLon) && geo::distanceM(centerLat, centerLon, fixLat, fixLon) > TRACK_RANGE_M)
    return Start::TooFar;
  return Start::Ok;
}

}  // namespace trackrange

#endif
