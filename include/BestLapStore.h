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

}  // namespace BestLapStore

#endif  // BEST_LAP_STORE_H
