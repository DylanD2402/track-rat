/**
 * Track Rat - Performance run log
 *
 * Writes the session's acceleration and braking runs to /sessions/<stamp>_perf.json
 * on the SD card (or internal flash if there is no card). Rewritten after every run.
 */

#ifndef PERF_LOG_H
#define PERF_LOG_H

#include <Arduino.h>
#include "PerfRun.h"

namespace PerfLog {

// Picks a file name for a new performance session. `stamp` may be empty if the GPS date is unknown.
String newPath(const String& stamp);

// Writes every run so far plus the session's best. Times are seconds, speeds km/h, distances metres.
bool save(const String& path, const String& stamp, const perf::PerfRun& runs);

}  // namespace PerfLog

#endif  // PERF_LOG_H
