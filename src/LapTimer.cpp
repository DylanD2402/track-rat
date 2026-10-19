/**
 * Track Rat - Lap Timer Implementation
 */

#include "LapTimer.h"
#include "Config.h"

void LapTimer::begin(const TrackPoint& startFinish, const std::vector<TrackPoint>& splits, uint32_t minLapMs) {
  sf = startFinish;
  minLap = minLapMs;
  splitPts = splits;
  reset();
}

void LapTimer::changeGates(const TrackPoint& startFinish, const std::vector<TrackPoint>& splits) {
  sf = startFinish;
  splitPts = splits;
  sfHeadingConfirmed = false;
  sfWrongWay = false;
  if (running) lapSplits.assign(splitPts.size(), -1);
}

void LapTimer::reset() {
  laps.clear();
  laps.reserve(64);
  bestIndex = -1;
  havePrev = false;
  haveTimeBase = false;
  dayOffset = 0;
  running = false;
  lapNumber = 0;
  sessionMaxSpeed = 0;
  curTrace.clear();
  bestTrace.clear();
  matchIdx = 0;
  liveDelta = NO_DELTA;
  sfHeadingConfirmed = false;
  sfWrongWay = false;
}

int64_t LapTimer::continuousTime(uint32_t gpsTimeMs) {
  if (haveTimeBase && gpsTimeMs + 43200000UL < lastRawGps) {
    dayOffset += 86400000LL;  // passed UTC midnight
  }
  haveTimeBase = true;
  lastRawGps = gpsTimeMs;
  return dayOffset + gpsTimeMs;
}

void LapTimer::startLap(int64_t t, uint32_t startMillis) {
  running = true;
  lapNumber++;
  lapStartT = t;
  lapStartMillis = startMillis;
  lapMaxSpeed = 0;
  lapSpeedSum = 0;
  lapSpeedCount = 0;
  lapSplits.assign(splitPts.size(), -1);
  curTrace.clear();
  curTrace.reserve(2048);
  curTrace.push_back({0, 0, 0});  // the start/finish is the origin
  matchIdx = 0;
  liveDelta = NO_DELTA;
}

// Records the splits crossed on the segment from the previous fix to (lat, lon), whose crossing
// fraction is above `after` and at most `upTo`, in the order they were crossed. Called once for the
// part of the segment before the start/finish and once for the part after it.
void LapTimer::crossSplits(double lat, double lon, int64_t dt, double after, double upTo, LapEvents& out) {
  for (;;) {
    int next = -1;
    double nextFrac = 0;
    for (size_t i = 0; i < splitPts.size(); i++) {
      if (lapSplits[i] >= 0) continue;  // already crossed this lap
      const TrackPoint& sp = splitPts[i];
      geo::XY a = geo::toLocal(prevLat, prevLon, sp.lat, sp.lon);
      geo::XY b = geo::toLocal(lat, lon, sp.lat, sp.lon);
      double frac;
      if (!geo::crossesGate(a, b, sp.radius, frac) || frac <= after || frac > upTo) continue;
      if (sp.hasHeading() && !geo::travelsWith(a, b, sp.heading)) continue;
      if (next < 0 || frac < nextFrac) {  // equal fractions: the lower split first
        next = (int)i;
        nextFrac = frac;
      }
    }
    if (next < 0) return;

    int64_t tc = prevT + (int64_t)(nextFrac * dt + 0.5);
    lapSplits[next] = (int32_t)(tc - lapStartT);
    LapEvent ev;
    ev.type = LapEventType::SplitCrossed;
    ev.splitIndex = next;
    ev.timeMs = lapSplits[next];
    const Lap* best = bestLap();
    if (best && (size_t)next < best->splitMs.size() && best->splitMs[next] >= 0) {
      ev.deltaMs = lapSplits[next] - best->splitMs[next];
    }
    out.add(ev);
  }
}

LapEvents LapTimer::onFix(double lat, double lon, float speedKmh, uint32_t gpsTimeMs, uint32_t rxMillis) {
  LapEvents evs;
  int64_t t = continuousTime(gpsTimeMs);

  if (speedKmh > sessionMaxSpeed) sessionMaxSpeed = speedKmh;

  int64_t dt = t - prevT;
  bool usable = havePrev && dt > 0 && dt <= MAX_FIX_GAP_MS;

  if (usable) {
    bool moving = (speedKmh >= MIN_CROSSING_SPEED_KMH || prevSpeed >= MIN_CROSSING_SPEED_KMH);

    // A segment can cross splits and the start/finish together, so find where the line is first:
    // splits up to it belong to the lap that's ending (with the directions as they were),
    // splits after it to the lap that starts (with the directions as the line left them).
    geo::XY a = geo::toLocal(prevLat, prevLon, sf.lat, sf.lon);
    geo::XY b = geo::toLocal(lat, lon, sf.lat, sf.lon);
    double frac = 0;
    bool lineCrossed = moving && geo::crossesGate(a, b, sf.radius, frac);

    if (running) {
      if (speedKmh > lapMaxSpeed) lapMaxSpeed = speedKmh;
      lapSpeedSum += speedKmh;
      lapSpeedCount++;
      // A split at the very same point as the line counts as before it: it was checked first before this too.
      if (moving) crossSplits(lat, lon, dt, -1.0, lineCrossed ? frac : 2.0, evs);
    }

    if (lineCrossed && startFinishCounts(a, b, frac)) {
      int64_t tc = prevT + (int64_t)(frac * dt + 0.5);
      uint32_t tcMillis = rxMillis - (uint32_t)(t - tc);

      if (!running) {
        startLap(tc, tcMillis);
        LapEvent ev;
        ev.type = LapEventType::LapStarted;
        evs.add(ev);
      } else {
        int64_t elapsed = tc - lapStartT;
        if (elapsed >= minLap) {
          LapEvent ev;
          if (elapsed <= MAX_LAP_TIME_MS && laps.size() < MAX_LAPS_STORED) {
            Lap lap;
            lap.number = lapNumber;
            lap.timeMs = (uint32_t)elapsed;
            lap.maxSpeedKmh = lapMaxSpeed;
            lap.avgSpeedKmh = lapSpeedCount ? (float)(lapSpeedSum / lapSpeedCount) : 0;
            lap.splitMs = lapSplits;

            const Lap* prevBest = bestLap();
            if (prevBest) ev.deltaMs = (int32_t)lap.timeMs - (int32_t)prevBest->timeMs;
            laps.push_back(lap);
            if (!prevBest || lap.timeMs < prevBest->timeMs) {
              bestIndex = laps.size() - 1;
              ev.isBest = true;
              curTrace.push_back({0, 0, (int32_t)lap.timeMs});
              bestTrace.swap(curTrace);  // this lap's path becomes the reference
            }
            ev.type = LapEventType::LapCompleted;
            ev.timeMs = lap.timeMs;
          } else {
            ev.type = LapEventType::LapStarted;  // too long (pit stop etc.) - not recorded
          }
          evs.add(ev);
          startLap(tc, tcMillis);
        }
        // else: crossed again too soon (GPS jitter at the line) - ignore
      }
    }

    // The rest of the segment, in whichever lap is running now (the same one if the line didn't count).
    if (lineCrossed && running && moving) crossSplits(lat, lon, dt, frac, 2.0, evs);
  }

  if (running) {
    int32_t elapsed = (int32_t)(t - lapStartT);
    geo::XY p = geo::toLocal(lat, lon, sf.lat, sf.lon);
    if (curTrace.size() < MAX_TRACE_POINTS) curTrace.push_back({(float)p.x, (float)p.y, elapsed});
    liveDelta = matchBestTrace((float)p.x, (float)p.y, elapsed);
  }

  havePrev = true;
  prevLat = lat;
  prevLon = lon;
  prevT = t;
  prevSpeed = speedKmh;
  return evs;
}

// Direction check for a start/finish crossing (see LapTimer.h).
bool LapTimer::startFinishCounts(geo::XY a, geo::XY b, double frac) {
  if (!sf.hasHeading()) return true;
  if (geo::travelsWith(a, b, sf.heading)) {
    sfHeadingConfirmed = true;
    sfWrongWay = false;
    return true;
  }
  if (sf.headingFrom != HeadingFrom::Map || sfHeadingConfirmed) return false;

  geo::XY at = {a.x + frac * (b.x - a.x), a.y + frac * (b.y - a.y)};
  bool samePlace = sfWrongWay && hypot(at.x - sfWrongAt.x, at.y - sfWrongAt.y) < 6.0;
  sfWrongWay = true;
  sfWrongAt = at;
  if (!samePlace) return false;

  // Crossed the line itself the wrong way twice, with a guessed direction: it's backwards.
  sf.heading = fmodf(sf.heading + 180.0f, 360.0f);
  for (TrackPoint& sp : splitPts)
    if (sp.headingFrom == HeadingFrom::Map) sp.heading = fmodf(sp.heading + 180.0f, 360.0f);
  sfHeadingConfirmed = true;
  sfWrongWay = false;
  return true;
}

void LapTimer::startLapNow(uint32_t gpsTimeMs, uint32_t rxMillis) {
  startLap(continuousTime(gpsTimeMs), rxMillis);
}

uint32_t LapTimer::currentLapMs(uint32_t nowMillis) const {
  if (!running) return 0;
  return nowMillis - lapStartMillis;
}

uint32_t LapTimer::averageLapMs() const {
  if (laps.empty()) return 0;
  uint64_t total = 0;
  for (const Lap& l : laps) total += l.timeMs;
  return total / laps.size();
}

// Finds where on the best lap's path we are now, and how long the best lap
// took to get there. Searches a window around the last match so the start
// and end of the lap (both near the start/finish) don't get confused.
int32_t LapTimer::matchBestTrace(float x, float y, int32_t elapsed) {
  const size_t n = bestTrace.size();
  if (n < 2) return NO_DELTA;

  auto search = [&](size_t from, size_t to, float& bestD2, size_t& bestI, float& bestU) {
    for (size_t i = from; i < to && i + 1 < n; i++) {
      const TracePt& a = bestTrace[i];
      const TracePt& b = bestTrace[i + 1];
      float dx = b.x - a.x, dy = b.y - a.y;
      float len2 = dx * dx + dy * dy;
      float u = len2 > 1e-6f ? ((x - a.x) * dx + (y - a.y) * dy) / len2 : 0;
      if (u < 0) u = 0;
      if (u > 1) u = 1;
      float px = a.x + u * dx - x, py = a.y + u * dy - y;
      float d2 = px * px + py * py;
      if (d2 < bestD2) {
        bestD2 = d2;
        bestI = i;
        bestU = u;
      }
    }
  };

  const float maxD2 = LIVE_DELTA_MAX_OFFSET_M * LIVE_DELTA_MAX_OFFSET_M;
  float bestD2 = 1e30f;
  size_t bestI = 0;
  float bestU = 0;
  size_t from = matchIdx > 20 ? matchIdx - 20 : 0;
  search(from, matchIdx + 200, bestD2, bestI, bestU);
  if (bestD2 > maxD2) {  // lost track of where we are: search the whole lap
    search(0, n, bestD2, bestI, bestU);
    if (bestD2 > maxD2) return NO_DELTA;
  }

  matchIdx = bestI;
  const TracePt& a = bestTrace[bestI];
  const TracePt& b = bestTrace[bestI + 1];
  int32_t refT = a.t + (int32_t)(bestU * (b.t - a.t) + 0.5f);
  return elapsed - refT;
}
