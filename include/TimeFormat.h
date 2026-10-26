/**
 * Track Rat - lap time and delta text
 *
 * Pure text, no Arduino dependencies, so it can be unit-tested on a PC
 * (test/test_timeformat.cpp). SessionLog.cpp wraps these in Strings.
 */

#ifndef TIME_FORMAT_H
#define TIME_FORMAT_H

#include <stdint.h>
#include <stdio.h>

namespace timefmt {

constexpr int32_t NO_DELTA_MS = INT32_MIN;  // LapTimer.h's NO_DELTA: no reference to compare with

// "1:23.456" (decimals 3), "1:23.4" (1), "1:23" (0). buf of at least 24 bytes.
inline void lapTime(char* buf, size_t size, uint32_t ms, int decimals = 3) {
  const unsigned long minutes = ms / 60000, seconds = (ms / 1000) % 60, frac = ms % 1000;
  if (decimals <= 0) snprintf(buf, size, "%lu:%02lu", minutes, seconds);
  else if (decimals == 1) snprintf(buf, size, "%lu:%02lu.%01lu", minutes, seconds, frac / 100);
  else snprintf(buf, size, "%lu:%02lu.%03lu", minutes, seconds, frac);
}

// "+0.231", "-1.020", or "--" for no delta. buf of at least 16 bytes.
inline void delta(char* buf, size_t size, int32_t ms) {
  if (ms == NO_DELTA_MS) {
    snprintf(buf, size, "--");
    return;
  }
  const uint32_t a = ms < 0 ? 0u - (uint32_t)ms : (uint32_t)ms;
  snprintf(buf, size, "%c%lu.%03lu", ms < 0 ? '-' : '+', (unsigned long)(a / 1000), (unsigned long)(a % 1000));
}

}  // namespace timefmt

#endif  // TIME_FORMAT_H
