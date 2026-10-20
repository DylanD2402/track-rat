/**
 * Track Rat - theoretical best and the pit debrief
 *
 * From the laps of a session with corner sectors (Sectors.h):
 *
 * - Each lap's sector times: T1 from the line to the first gate, ..., the
 *   last from the last gate to the line. They add up to the lap time.
 * - The best time in each sector, from clean laps only (LapTags.h), and the
 *   lap it came from. Their sum is the theoretical best.
 * - The gap in each sector between the best lap and the best in that
 *   sector. The gaps add up exactly to best lap - theoretical best.
 * - The three sectors with the biggest gaps, each with one hint from
 *   comparing the two laps' GPS speed through it: where they started
 *   braking, their slowest speed, and their speed at the exit.
 *
 * The all-time theoretical best uses the best sector times ever at the
 * track (kept with the best laps, BestLap.h), merged with this session's.
 *
 * No Arduino dependencies: tested on a PC in test/test_debrief.cpp.
 */

#ifndef DEBRIEF_H
#define DEBRIEF_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "GeoMath.h"
#include "LapTags.h"

namespace debrief {

// A lap as the debrief needs it.
struct LapIn {
  uint16_t number;
  uint32_t timeMs;
  uint8_t tags;
  std::vector<int32_t> gateMs;  // elapsed at each sector gate (Lap::sectorMs), -1 if missed
};

// Sector times of a lap with `sectors` sectors (gates + 1): empty unless
// every gate was crossed, in order.
inline std::vector<int32_t> sectorTimes(uint32_t lapMs, const std::vector<int32_t>& gateMs, size_t sectors) {
  std::vector<int32_t> out;
  if (sectors < 2 || gateMs.size() + 1 != sectors) return out;
  int32_t prev = 0;
  for (int32_t g : gateMs) {
    if (g <= prev) return std::vector<int32_t>();
    out.push_back(g - prev);
    prev = g;
  }
  if ((int32_t)lapMs <= prev) return std::vector<int32_t>();
  out.push_back((int32_t)lapMs - prev);
  return out;
}

struct SectorBest {  // a plain aggregate: the firmware builds as C++11
  int32_t ms;    // -1: none
  uint16_t lap;  // the lap it came from
};

struct Result {
  bool valid = false;            // a clean lap with every sector, so there's something to say
  size_t sectors = 0;
  int bestIndex = -1;            // into the laps: the best clean lap timed through every sector
  uint32_t bestMs = 0;
  uint32_t theoreticalMs = 0;    // this session
  std::vector<SectorBest> best;  // per sector
  std::vector<int32_t> gaps;     // best lap - best in that sector, per sector (>= 0)
  std::vector<int> top;          // up to three sectors with the biggest gaps (> 0), biggest first
  int lapsUsed = 0;              // clean laps with every sector
  int lapsLeftOut = 0;           // the rest
};

inline Result analyse(const std::vector<LapIn>& laps, size_t sectors) {
  Result r;
  r.sectors = sectors;
  r.best.assign(sectors, SectorBest{-1, 0});
  for (size_t i = 0; i < laps.size(); i++) {
    const LapIn& l = laps[i];
    std::vector<int32_t> st = sectorTimes(l.timeMs, l.gateMs, sectors);
    if (l.tags != 0 || st.empty()) {
      r.lapsLeftOut++;
      continue;
    }
    r.lapsUsed++;
    for (size_t s = 0; s < sectors; s++)
      if (r.best[s].ms < 0 || st[s] < r.best[s].ms) r.best[s] = {st[s], l.number};
    if (r.bestIndex < 0 || l.timeMs < r.bestMs) {
      r.bestIndex = (int)i;
      r.bestMs = l.timeMs;
    }
  }
  if (r.bestIndex < 0) return r;
  r.valid = true;
  std::vector<int32_t> bestLap = sectorTimes(laps[r.bestIndex].timeMs, laps[r.bestIndex].gateMs, sectors);
  for (size_t s = 0; s < sectors; s++) {
    r.theoreticalMs += r.best[s].ms;
    r.gaps.push_back(bestLap[s] - r.best[s].ms);
  }
  std::vector<int> order;
  for (size_t s = 0; s < sectors; s++)
    if (r.gaps[s] > 0) order.push_back((int)s);
  for (size_t a = 0; a < order.size(); a++)  // biggest gap first; equal gaps: the earlier sector
    for (size_t b = a + 1; b < order.size(); b++)
      if (r.gaps[order[b]] > r.gaps[order[a]]) std::swap(order[a], order[b]);
  if (order.size() > 3) order.resize(3);
  r.top = order;
  return r;
}

// The all-time best in each sector: the stored ones (-1: none) with this
// session's merged in. A list of another length is from other gates: it
// starts again from this session's.
inline std::vector<int32_t> mergeBest(const std::vector<int32_t>& stored, const Result& session) {
  std::vector<int32_t> out(session.sectors, -1);
  for (size_t s = 0; s < session.sectors; s++) {
    int32_t a = stored.size() == session.sectors ? stored[s] : -1;
    int32_t b = session.best.size() > s ? session.best[s].ms : -1;
    out[s] = a < 0 ? b : (b < 0 ? a : (a < b ? a : b));
  }
  return out;
}
// Are these the gates the stored sector times were set through? (Each
// within 5 m, the same number.) Gates made again elsewhere start afresh.
inline bool sameGates(const std::vector<geo::XY>& storedLatLon, const std::vector<TrackPoint>& gates) {
  if (storedLatLon.size() != gates.size()) return false;
  for (size_t i = 0; i < gates.size(); i++)
    if (geo::distanceM(storedLatLon[i].x, storedLatLon[i].y, gates[i].lat, gates[i].lon) > 5.0) return false;
  return true;
}

// Their sum, or 0 if any sector has none.
inline uint32_t sumOf(const std::vector<int32_t>& best) {
  uint32_t sum = 0;
  for (int32_t b : best) {
    if (b < 0) return 0;
    sum += b;
  }
  return best.empty() ? 0 : sum;
}

// --- Hints from the speed traces ---------------------------------------------

// A lap's way through one sector: distance from the sector's start (m) and speed (km/h).
struct Trace {
  std::vector<float> distM, speedKmh;
};

// Builds a Trace from points with x, y (m), t (ms), speedKmh, between two
// times into the lap (the sector's start and end gates).
template <typename Pt>
Trace slice(const std::vector<Pt>& path, int32_t fromMs, int32_t toMs) {
  Trace tr;
  float d = 0;
  const Pt* prev = nullptr;
  for (const Pt& p : path) {
    if (p.t < fromMs || p.t > toMs) continue;
    if (prev) d += hypotf(p.x - prev->x, p.y - prev->y);
    tr.distM.push_back(d);
    tr.speedKmh.push_back(p.speedKmh);
    prev = &p;
  }
  return tr;
}

enum class HintKind { None, Braking, MinSpeed, Exit };
struct Hint {
  HintKind kind;
  float value;  // Braking: m earlier (+) than the best; MinSpeed / Exit: km/h slower (+)
};

// Where braking starts: the first point after the sector's fastest point
// (it starts at the fastest point before a braking zone) where the speed
// is BRAKE_START_KMH below it. -1 if it never brakes.
constexpr float BRAKE_START_KMH = 5.0f;
inline float brakingPoint(const Trace& t) {
  size_t peak = 0;
  for (size_t i = 0; i < t.speedKmh.size(); i++) {
    if (t.speedKmh[i] >= t.speedKmh[peak]) peak = i;
    else if (t.speedKmh[i] <= t.speedKmh[peak] - BRAKE_START_KMH) return t.distM[i];
  }
  return -1;
}
inline float minSpeed(const Trace& t) {
  float m = 1e9f;
  for (float v : t.speedKmh) m = v < m ? v : m;
  return t.speedKmh.empty() ? -1 : m;
}

// The one thing that cost the most time in this sector on `mine` against
// `best` (the lap with the best time there). Each difference is weighed by
// roughly what it costs: braking 5 m early about as much as 2 km/h less
// through the corner or onto the next straight. None if neither trace
// says anything, or `mine` wasn't worse at any of them.
inline Hint hint(const Trace& mine, const Trace& best) {
  Hint h = {HintKind::None, 0};
  if (mine.speedKmh.size() < 3 || best.speedKmh.size() < 3) return h;
  float score = 0;
  const float bm = brakingPoint(mine), bb = brakingPoint(best);
  if (bm >= 0 && bb >= 0 && (bb - bm) / 5.0f > score) {
    score = (bb - bm) / 5.0f;
    h = {HintKind::Braking, bb - bm};
  }
  const float dMin = minSpeed(best) - minSpeed(mine);
  if (dMin / 2.0f > score) {
    score = dMin / 2.0f;
    h = {HintKind::MinSpeed, dMin};
  }
  const float dExit = best.speedKmh.back() - mine.speedKmh.back();
  if (dExit / 2.0f > score) {
    score = dExit / 2.0f;
    h = {HintKind::Exit, dExit};
  }
  if (score < 0.5f) h = {HintKind::None, 0};  // under 2.5 m or 1 km/h: nothing worth saying
  return h;
}

// "braked 8 m early", "4 km/h slower at the apex" (the slowest point), "3 km/h slower on exit"
inline std::string hintText(const Hint& h, bool mph = false) {
  char buf[48];
  const float v = mph ? h.value / 1.609344f : h.value;
  const char* unit = mph ? "mph" : "km/h";
  switch (h.kind) {
    case HintKind::Braking: snprintf(buf, sizeof(buf), "braked %.0f m early", h.value); break;
    case HintKind::MinSpeed: snprintf(buf, sizeof(buf), "%.0f %s slower at the apex", v, unit); break;
    case HintKind::Exit: snprintf(buf, sizeof(buf), "%.0f %s slower on exit", v, unit); break;
    default: return std::string();
  }
  return buf;
}

// "0.31" for a gap in ms (seconds, two decimals)
inline std::string seconds(int32_t ms) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%s%d.%02d", ms < 0 ? "-" : "", abs(ms) / 1000, (abs(ms) % 1000 + 5) / 10 % 100);
  if ((abs(ms) % 1000 + 5) >= 1000) snprintf(buf, sizeof(buf), "%s%d.00", ms < 0 ? "-" : "", abs(ms) / 1000 + 1);
  return buf;
}

}  // namespace debrief

#endif  // DEBRIEF_H
