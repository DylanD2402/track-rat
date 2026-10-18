/**
 * Track Rat - Session log
 *
 * Writes the current session to /sessions/<date>_<time>_<track>.json on the
 * SD card (or internal flash if no card). Rewritten after every lap so a
 * power cut loses at most the lap in progress.
 */

#ifndef SESSION_LOG_H
#define SESSION_LOG_H

#include <Arduino.h>
#include "LapTimer.h"
#include "TrackDatabase.h"

namespace SessionLog {

// Picks a file name for a new session. `stamp` may be empty if GPS date is unknown.
String newSessionPath(const Track& track, const String& stamp);

bool save(const String& path, const Track& track, const String& stamp, const LapTimer& timer);

}  // namespace SessionLog

String formatLapTime(uint32_t ms, int decimals = 3);  // "1:23.456"
String formatDelta(int32_t ms);                       // "+0.231" / "-1.020"

#endif  // SESSION_LOG_H
