/**
 * Track Rat - Session log
 *
 * Each session is two files in /sessions, with the same base name:
 *
 * - <date>_<time>_<track>.vbo: every GPS fix (SD card only), in Racelogic's
 *   VBO format for Circuit Tools. Written in the background (LogWriter) and
 *   flushed about once a second, so a power cut loses at most a second.
 * - <date>_<time>_<track>.json: the lap summary, rewritten after every lap
 *   (temporary file, then rename, so a power cut can't leave it half
 *   written) and marked finished when the session ends.
 *
 * Free Roam drives are <date>_<time>_freeroam.vbo/.json the same way.
 * Without an SD card only the summaries are kept, in flash.
 */

#ifndef SESSION_LOG_H
#define SESSION_LOG_H

#include <Arduino.h>
#include "FreeRoam.h"
#include "GPSHandler.h"
#include "LapTimer.h"
#include "TrackDatabase.h"

namespace SessionLog {

// Starts the background log writer if there's an SD card (after Storage::begin).
void begin();

// An unused base name for a new session's files: "/sessions/<stamp>_<name>"
// with no extension (neither the .json nor the .vbo exists). `stamp` may be
// empty if the GPS date is unknown. "" if there's no storage.
String newSessionBase(const String& stamp, const String& name);

// The lap summary. `logName` is the session's .vbo file name ("" if none);
// `finished` marks a session that was ended, not just saved after a lap.
bool save(const String& path, const Track& track, const String& stamp, const LapTimer& timer,
          const String& logName, bool finished, bool recovered = false);

// Free Roam summary: time, distance and speeds. Includes the route (a point
// every ~10 m) only when there's no full log.
bool saveFreeRoam(const String& path, const String& stamp, const FreeRoam& drive, uint32_t durationMs,
                  const String& logName, bool recovered = false);

// --- Full-rate log (.vbo) ---------------------------------------------------

// True when every fix can be logged: an SD card and the writer task.
bool fullLogAvailable();

// Start a log; `first` is the session's first fix (date and time for the
// header). `timing`: the start/finish line is known now, so it's recorded
// in the header (and as a Circuit Tools timing line if the map shows which
// way the track runs there).
bool beginTrackLog(const String& vboPath, const Track& track, const String& stamp, bool timing,
                   uint32_t minLapMs, const GpsFix& first);
bool beginFreeRoamLog(const String& vboPath, const String& stamp, const GpsFix& first);
void logFix(const GpsFix& fix, uint16_t lap);
void endLog();
bool logging();
uint32_t fixesLogged();  // in the current (or last) log

// At boot: any session whose log has no finished summary (the power went
// off mid-session) gets its summary rebuilt from the log.
void recoverUnfinished(const TrackDatabase& tracks);

String fileName(const String& path);  // "/sessions/a.vbo" -> "a.vbo"

}  // namespace SessionLog

String formatLapTime(uint32_t ms, int decimals = 3);  // "1:23.456"
String formatDelta(int32_t ms);                       // "+0.231" / "-1.020"

#endif  // SESSION_LOG_H
