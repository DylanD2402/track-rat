/**
 * Track Rat - Bench Test circuit (simulated car)
 *
 * A 60 m radius circle (~380 m lap, ~15 s per lap). The start/finish is
 * at angle 0 (east side of the circle); one split sits on the opposite side.
 */

#ifndef SIM_TRACK_H
#define SIM_TRACK_H

#define SIM_RADIUS_M 60.0
#define SIM_MIN_LAP_MS 8000  // bench laps are shorter than the normal MIN_LAP_TIME_MS
#define SIM_SF_LAT 37.000000
#define SIM_SF_LON -122.000000
// Split at angle pi: 2 * radius west of the start/finish
#define SIM_SPLIT_LAT SIM_SF_LAT
#define SIM_SPLIT_LON (SIM_SF_LON - (2.0 * SIM_RADIUS_M) / (111320.0 * 0.7986355))  // cos(37 deg)

#endif  // SIM_TRACK_H
