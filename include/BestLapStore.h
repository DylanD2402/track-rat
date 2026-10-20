/**
 * Track Rat - best laps on the card
 *
 * Reads and writes the best-ever (best.bin) and best-of-the-day (day.bin)
 * laps of each track, in /tracks/<track id>/ on the session storage (the SD
 * card, or flash without one). The format and the rules are in BestLap.h.
 * Saved the safe way: a temporary file, then a rename, so a power cut keeps
 * either the old lap or the new one.
 */

#ifndef BEST_LAP_STORE_H
#define BEST_LAP_STORE_H

#include <Arduino.h>
#include "BestLap.h"

namespace BestLapStore {

enum class Kind { Ever, Day };

// withPath false reads only the header (time, date, line): for the screens.
bool load(const String& trackId, Kind kind, bestlap::Record& out, bool withPath = true);
bool save(const String& trackId, Kind kind, const bestlap::Record& r);

// The rest kept per track for the debrief (stats.json): the best time ever
// in each corner sector, with the gates they were set through, and the last
// session's number, date and best lap. The best in each sector on the day
// too, for the timing screen's sector strip.
struct Stats {
  std::vector<geo::XY> sectorGates;  // x = lat, y = lon
  std::vector<int32_t> sectorBestMs;
  uint32_t dayDate = 0;               // YYYYMMDD of daySectorBestMs
  std::vector<int32_t> daySectorBestMs;
  int lastSession = 0;
  uint32_t lastDate = 0;    // YYYYMMDD
  uint32_t lastBestMs = 0;  // 0: no lap
};
bool loadStats(const String& trackId, Stats& out);
bool saveStats(const String& trackId, const Stats& s);

}  // namespace BestLapStore

#endif  // BEST_LAP_STORE_H
