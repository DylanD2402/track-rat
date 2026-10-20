/**
 * Track Rat - best-ever and best-of-the-day laps
 *
 * For each track (each layout has its own id) two laps are kept on the
 * storage card, in /tracks/<track id>/:
 *
 * - best.bin: the personal best (PB), the fastest clean lap ever there.
 * - day.bin: the fastest clean lap of its local date.
 *
 * Each is the lap's path (x, y in metres from the start/finish, ms into the
 * lap, speed), its time, its local date and where the start/finish was. A
 * record whose start/finish has since moved (SET LINE) no longer counts:
 * its times were measured at another line. About 16 bytes a point, so a
 * 90 s lap at 20 Hz is about 30 KB.
 *
 * The Compare with setting picks which one the live delta starts from:
 * best ever, or best of the day (best ever until a lap is set today), or
 * neither (this session only). Only clean laps (no tags, see LapTags.h)
 * are ever stored.
 *
 * No Arduino dependencies: tested on a PC in test/test_bestlap.cpp.
 */

#ifndef BEST_LAP_H
#define BEST_LAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "GeoMath.h"
#include "SettingsData.h"

namespace bestlap {

struct Point {
  float x, y;      // metres from the start/finish
  int32_t t;       // ms into the lap
  float speedKmh;
};

struct Record {
  uint32_t lapMs = 0;  // 0: none
  uint32_t date = 0;   // local YYYYMMDD, 0 if not known
  double sfLat = 0, sfLon = 0;
  std::vector<Point> path;
  bool valid() const { return lapMs > 0; }
};

constexpr uint32_t MAGIC = 0x31425254;  // "TRB1"
constexpr size_t HEADER_BYTES = 32;
constexpr size_t POINT_BYTES = 16;
constexpr size_t MAX_POINTS = 30000;     // as LapTimer's MAX_TRACE_POINTS
constexpr double SAME_LINE_M = 5.0;      // a start/finish moved further than this is another line

namespace detail {
inline void put32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i)));
}
inline uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline void putF(std::vector<uint8_t>& b, float f) {
  uint32_t v;
  memcpy(&v, &f, 4);
  put32(b, v);
}
inline float getF(const uint8_t* p) {
  uint32_t v = get32(p);
  float f;
  memcpy(&f, &v, 4);
  return f;
}
inline void putD(std::vector<uint8_t>& b, double d) {
  uint64_t v;
  memcpy(&v, &d, 8);
  put32(b, (uint32_t)v);
  put32(b, (uint32_t)(v >> 32));
}
inline double getD(const uint8_t* p) {
  uint64_t v = (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
  double d;
  memcpy(&d, &v, 8);
  return d;
}
}  // namespace detail

// The file: a 32-byte header (magic, lap ms, date, start/finish lat and
// lon, point count), then the points. Little-endian throughout.
inline std::vector<uint8_t> encode(const Record& r) {
  std::vector<uint8_t> b;
  const size_t n = r.path.size() < MAX_POINTS ? r.path.size() : MAX_POINTS;
  b.reserve(HEADER_BYTES + n * POINT_BYTES);
  detail::put32(b, MAGIC);
  detail::put32(b, r.lapMs);
  detail::put32(b, r.date);
  detail::putD(b, r.sfLat);
  detail::putD(b, r.sfLon);
  detail::put32(b, (uint32_t)n);
  for (size_t i = 0; i < n; i++) {
    const Point& p = r.path[i];
    detail::putF(b, p.x);
    detail::putF(b, p.y);
    detail::put32(b, (uint32_t)p.t);
    detail::putF(b, p.speedKmh);
  }
  return b;
}

// The point count a header says follows, or -1 if `head` (HEADER_BYTES) isn't a header.
inline long pointCount(const uint8_t* head, size_t len) {
  if (len < HEADER_BYTES || detail::get32(head) != MAGIC) return -1;
  uint32_t n = detail::get32(head + 28);
  return n <= MAX_POINTS ? (long)n : -1;
}

// Reads a file back. With only the header (len == HEADER_BYTES) and
// withPath false, fills in everything but the path. False if it's damaged
// or cut short (a lap with a lap time but no path is no use as a reference).
inline bool decode(const uint8_t* data, size_t len, Record& out, bool withPath = true) {
  long n = pointCount(data, len);
  if (n < 0) return false;
  Record r;
  r.lapMs = detail::get32(data + 4);
  r.date = detail::get32(data + 8);
  r.sfLat = detail::getD(data + 12);
  r.sfLon = detail::getD(data + 20);
  if (r.lapMs == 0) return false;
  if (withPath) {
    if (n < 2 || len < HEADER_BYTES + (size_t)n * POINT_BYTES) return false;
    r.path.resize(n);
    const uint8_t* p = data + HEADER_BYTES;
    for (long i = 0; i < n; i++, p += POINT_BYTES)
      r.path[i] = {detail::getF(p), detail::getF(p + 4), (int32_t)detail::get32(p + 8), detail::getF(p + 12)};
  }
  out = r;
  return true;
}

// "YYYYMMDD_HHMMSS" (a session stamp, local time) -> YYYYMMDD; 0 if it isn't one.
inline uint32_t dateFromStamp(const std::string& stamp) {
  if (stamp.size() < 9 || stamp[8] != '_') return 0;
  uint32_t d = 0;
  for (int i = 0; i < 8; i++) {
    if (stamp[i] < '0' || stamp[i] > '9') return 0;
    d = d * 10 + (stamp[i] - '0');
  }
  return d;
}

// Was this record timed at the start/finish that's at (lat, lon) now?
inline bool sameLine(const Record& r, double lat, double lon) {
  return r.valid() && geo::distanceM(r.sfLat, r.sfLon, lat, lon) <= SAME_LINE_M;
}

// Which stored lap the live delta starts from. `today` is the local date (0 if not known).
enum class Ref { None, Ever, Day };
inline Ref pickReference(CompareTo mode, const Record& ever, const Record& day, uint32_t today) {
  if (mode == CompareTo::Session) return Ref::None;
  if (mode == CompareTo::Day && day.valid() && today && day.date == today) return Ref::Day;
  return ever.valid() ? Ref::Ever : Ref::None;  // today: best ever until a lap is set today
}

// A clean lap of lapMs: does it beat the stored record? (Laps with tags never count.)
inline bool beats(uint32_t lapMs, uint8_t tags, const Record& stored) {
  return tags == 0 && lapMs > 0 && (!stored.valid() || lapMs < stored.lapMs);
}
// The same for the day's record: a record from another day doesn't count.
inline bool beatsDay(uint32_t lapMs, uint8_t tags, const Record& day, uint32_t today) {
  if (tags != 0 || lapMs == 0) return false;
  return !day.valid() || !today || day.date != today || lapMs < day.lapMs;
}

// "19 Oct 2026" for a YYYYMMDD date ("" if 0).
inline std::string dateText(uint32_t date) {
  static const char* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const uint32_t m = date / 100 % 100;
  if (date == 0 || m < 1 || m > 12) return std::string();
  char buf[16];
  snprintf(buf, sizeof(buf), "%u %s %u", (unsigned)(date % 100), MONTHS[m - 1], (unsigned)(date / 10000));
  return buf;
}

}  // namespace bestlap

#endif  // BEST_LAP_H
