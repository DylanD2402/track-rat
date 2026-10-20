/**
 * Track Rat - the delta-first timing screen's rules
 *
 * What the timing screen (DisplayManager::drawRunning) works out before it
 * draws, kept apart so it can be tested on a PC:
 *
 * - The delta colours: green ahead and red behind, or blue and orange (the
 *   Delta colours setting, for red-green colour blindness).
 * - The trend bar: how much the delta changed over the last TREND_WINDOW_MS.
 *   It grows up when gaining and down when losing, so it reads without colour.
 * - The sector strip: one block per corner sector of the lap under way.
 *   Purple: the best ever there. Green (blue): the best of the day. Yellow:
 *   slower.
 * - The flash at the line: purple for a PB, green (blue) for the best of
 *   the day, grey for any other lap.
 * - The ghost on the map: where the reference lap was this far into it.
 * - Taps while moving: a quick one is vibration or rain, only a firm press
 *   switches the view.
 *
 * No Arduino dependencies: tested in test/test_timing.cpp. C++11, like the firmware.
 */

#ifndef TIMING_SCREEN_H
#define TIMING_SCREEN_H

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include "Config.h"
#include "SettingsData.h"
#include "UiTheme.h"

namespace timing {

// --- colours -------------------------------------------------------------------

struct Colours {
  uint16_t ahead, behind;  // the delta band and trend bar; text on both is black
  uint16_t dayBest;        // a best of the day: sector blocks and the line flash
};
inline Colours colours(DeltaColours c) {
  if (c == DeltaColours::BlueOrange) return {ui::C_BLUE, ui::C_ORANGE, ui::C_BLUE};
  return {ui::C_GREEN, ui::C_RED, ui::C_GREEN};
}
// The band's colour for a delta (+ = slower). Level counts as ahead.
inline uint16_t deltaColour(int32_t deltaMs, DeltaColours c) {
  return deltaMs <= 0 ? colours(c).ahead : colours(c).behind;
}

// --- the trend bar ---------------------------------------------------------------

constexpr int32_t NO_TREND = INT32_MIN;

// The live delta over the last few seconds. Fed once per GPS fix with the
// fix's time; keeps a sample every SAMPLE_MS, so it never allocates.
class DeltaTrend {
public:
  static constexpr uint32_t SAMPLE_MS = 100;
  static constexpr size_t CAPACITY = TREND_WINDOW_MS / SAMPLE_MS + 4;

  void clear() { count = 0; }
  // A fix without a delta (off the reference line, no reference) starts again.
  void add(uint32_t tMs, int32_t deltaMs, bool hasDelta) {
    if (!hasDelta) return clear();
    if (count && tMs < newest().t) return clear();  // time went backwards (a new day, a replay)
    if (count && tMs - newest().t < SAMPLE_MS) {
      newest().delta = deltaMs;  // keep the newest value, but not a sample every fix
      return;
    }
    if (count && tMs - newest().t > 2 * TREND_WINDOW_MS) clear();  // a long gap: old samples mean nothing
    if (count == CAPACITY) {
      head = (head + 1) % CAPACITY;
      count--;
    }
    buf[(head + count) % CAPACITY] = {tMs, deltaMs};
    count++;
  }
  // How much the delta changed over about windowMs: + = losing time, - = gaining.
  // NO_TREND until there's at least a second of history.
  int32_t change(uint32_t windowMs = TREND_WINDOW_MS) const {
    if (count < 2) return NO_TREND;
    const Sample& now = newest();
    const Sample* from = &at(0);
    for (size_t i = 0; i < count; i++) {  // the oldest sample still inside the window
      if (now.t - at(i).t <= windowMs) {
        from = &at(i);
        break;
      }
    }
    if (now.t - from->t < 1000) return NO_TREND;
    return now.delta - from->delta;
  }

private:
  struct Sample {
    uint32_t t;
    int32_t delta;
  };
  Sample buf[CAPACITY];
  size_t head = 0, count = 0;
  const Sample& at(size_t i) const { return buf[(head + i) % CAPACITY]; }
  Sample& newest() { return buf[(head + count - 1) % CAPACITY]; }
  const Sample& newest() const { return buf[(head + count - 1) % CAPACITY]; }
};

// The bar's height, -1..1 of its full height: + (up) for gaining, - (down) for losing.
inline float trendFraction(int32_t changeMs, int32_t fullScaleMs = TREND_FULL_SCALE_MS) {
  if (changeMs == NO_TREND || fullScaleMs <= 0) return 0;
  float f = -(float)changeMs / (float)fullScaleMs;
  return f > 1 ? 1 : f < -1 ? -1 : f;
}

// --- the sector strip ----------------------------------------------------------------

enum class Mark : uint8_t { Pending, Missed, Ever, Day, Slower };

// One sector's time against the best ever and the best of the day there
// (-1: none yet). A time that equals or beats a best is that best.
inline Mark sectorMark(int32_t ms, int32_t bestEverMs, int32_t bestDayMs) {
  if (ms <= 0) return Mark::Missed;
  if (bestEverMs <= 0 || ms <= bestEverMs) return Mark::Ever;  // nothing to beat: it's the best there's been
  if (bestDayMs <= 0 || ms <= bestDayMs) return Mark::Day;
  return Mark::Slower;
}

// The lowest of two lists of best times, sector by sector (-1 or 0: none).
inline std::vector<int32_t> bestOf(const std::vector<int32_t>& a, const std::vector<int32_t>& b) {
  std::vector<int32_t> out(a.size() > b.size() ? a.size() : b.size(), -1);
  for (size_t i = 0; i < out.size(); i++) {
    const int32_t x = i < a.size() ? a[i] : -1, y = i < b.size() ? b[i] : -1;
    out[i] = x <= 0 ? (y > 0 ? y : -1) : (y <= 0 || x < y) ? x : y;
  }
  return out;
}

// The strip for the lap under way: gateMs is its elapsed time at each sector
// gate so far (-1: not yet, or missed), so `sectors` = gateMs.size() + 1. A
// sector is finished when its end gate is crossed; one before a crossed gate
// that has no time was missed. The last sector ends at the line, so on the
// lap under way it's always pending.
inline std::vector<Mark> sectorStrip(const std::vector<int32_t>& gateMs, const std::vector<int32_t>& bestEver,
                                     const std::vector<int32_t>& bestDay) {
  const size_t n = gateMs.size() + 1;
  std::vector<Mark> out(n, Mark::Pending);
  if (gateMs.empty()) return out;
  size_t lastCrossed = SIZE_MAX;
  for (size_t i = 0; i < gateMs.size(); i++)
    if (gateMs[i] >= 0) lastCrossed = i;
  if (lastCrossed == SIZE_MAX) return out;
  for (size_t i = 0; i <= lastCrossed; i++) {
    if (gateMs[i] < 0) {
      out[i] = Mark::Missed;
      continue;
    }
    // Its start: the line, or the gate before (which must have a time too)
    int32_t from = 0;
    if (i > 0) from = gateMs[i - 1];
    if (from < 0 || gateMs[i] <= from) {
      out[i] = Mark::Missed;
      continue;
    }
    out[i] = sectorMark(gateMs[i] - from, i < bestEver.size() ? bestEver[i] : -1, i < bestDay.size() ? bestDay[i] : -1);
  }
  return out;
}

inline uint16_t markColour(Mark m, DeltaColours c) {
  switch (m) {
    case Mark::Ever: return ui::C_PURPLE;
    case Mark::Day: return colours(c).dayBest;
    case Mark::Slower: return ui::C_YELLOW;
    case Mark::Missed: return ui::C_LINE;
    default: return ui::C_SURFACE;
  }
}

// --- the flash at the line ---------------------------------------------------------------

enum class LineFlash : uint8_t { Lap, DayBest, Pb };
inline LineFlash lineFlash(bool pb, bool dayBest) { return pb ? LineFlash::Pb : dayBest ? LineFlash::DayBest : LineFlash::Lap; }
inline uint16_t flashColour(LineFlash f, DeltaColours c) {
  return f == LineFlash::Pb ? ui::C_PURPLE : f == LineFlash::DayBest ? colours(c).dayBest : ui::C_RAISED;
}
inline uint16_t flashTextColour(LineFlash f) { return f == LineFlash::Lap ? ui::C_TEXT : ui::C_BG; }

// --- the ghost ------------------------------------------------------------------------------

// Where a lap's path (points with float x, y and int32_t t, ms into the lap,
// in time order) was at elapsedMs, between its two nearest points. False
// past either end, or with fewer than two points.
template <typename P>
inline bool ghostAt(const std::vector<P>& path, int32_t elapsedMs, float& x, float& y) {
  if (path.size() < 2 || elapsedMs < path.front().t || elapsedMs > path.back().t) return false;
  size_t lo = 0, hi = path.size() - 1;  // path[lo].t <= elapsed <= path[hi].t
  while (hi - lo > 1) {
    const size_t mid = (lo + hi) / 2;
    if (path[mid].t <= elapsedMs) lo = mid;
    else hi = mid;
  }
  const P& a = path[lo];
  const P& b = path[hi];
  const float u = b.t > a.t ? (float)(elapsedMs - a.t) / (float)(b.t - a.t) : 0.0f;
  x = a.x + (b.x - a.x) * u;
  y = a.y + (b.y - a.y) * u;
  return true;
}

// --- taps while moving --------------------------------------------------------------------------

// Does a tap on the timing screen switch the view? Not a quick one while the
// car is moving (no fix: the speed is unknown, so it counts).
inline bool tapSwitchesView(bool hasFix, float speedKmh, uint32_t pressMs) {
  if (!hasFix || speedKmh < TAP_MOVING_KMH) return true;
  return pressMs >= FIRM_PRESS_MS;
}

}  // namespace timing

#endif  // TIMING_SCREEN_H
