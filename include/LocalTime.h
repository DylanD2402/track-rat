/**
 * Track Rat - local time for file names
 *
 * The GPS gives UTC. Session files are named in local time once a phone on
 * the Wi-Fi page has told Track Rat its time zone (minutes east of UTC).
 * Pure C++ (no Arduino); tested in test/test_localtime.cpp.
 */

#ifndef LOCAL_TIME_H
#define LOCAL_TIME_H

#include <stdint.h>
#include <stdio.h>

namespace localtime_ {

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
inline int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const int32_t yoe = y - era * 400;
  const int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

inline void civilFromDays(int32_t z, int& y, int& m, int& d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int32_t doe = z - era * 146097;
  const int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int32_t mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = yoe + era * 400 + (m <= 2);
}

// "YYYYMMDD_HHMMSS" for a UTC date and time shifted by offsetMin (minutes
// east of UTC, e.g. -240 for Ontario in summer). Rolls the date over as needed.
inline void stamp(int y, int mo, int d, int h, int mi, int s, int offsetMin, char* buf, size_t len) {
  int64_t t = ((int64_t)daysFromCivil(y, mo, d) * 86400) + h * 3600 + mi * 60 + s + (int64_t)offsetMin * 60;
  int32_t days = (int32_t)(t >= 0 ? t / 86400 : (t - 86399) / 86400);
  int32_t secs = (int32_t)(t - (int64_t)days * 86400);
  int ly, lm, ld;
  civilFromDays(days, ly, lm, ld);
  snprintf(buf, len, "%04d%02d%02d_%02d%02d%02d", ly, lm, ld, (int)(secs / 3600), (int)(secs / 60 % 60),
           (int)(secs % 60));
}

}  // namespace localtime_

#endif  // LOCAL_TIME_H
