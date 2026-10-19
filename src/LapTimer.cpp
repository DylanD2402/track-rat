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

LapEvent LapTimer::onFix(double lat, double lon, float speedKmh, uint32_t gpsTimeMs, uint32_t rxMillis) {
  LapEvent ev;
  int64_t t = continuousTime(gpsTimeMs);

  if (speedKmh > sessionMaxSpeed) sessionMaxSpeed = speedKmh;

  int64_t dt = t - prevT;
  bool usable = havePrev && dt > 0 && dt <= MAX_FIX_GAP_MS;

  if (usable) {
    bool moving = (speedKmh >= MIN_CROSSING_SPEED_KMH || prevSpeed >= MIN_CROSSING_SPEED_KMH);

    if (running) {
      if (speedKmh > lapMaxSpeed) lapMaxSpeed = speedKmh;
      lapSpeedSum += speedKmh;
      lapSpeedCount++;

      for (size_t i = 0; i < splitPts.size() && moving; i++) {
        if (lapSplits[i] >= 0) continue;  // already crossed this lap
        const TrackPoint& sp = splitPts[i];
        double frac;
        if (geo::crossesGate(geo::toLocal(prevLat, prevLon, sp.lat, sp.lon),
                             geo::toLocal(lat, lon, sp.lat, sp.lon), sp.radius, frac)) {
          int64_t tc = prevT + (int64_t)(frac * dt + 0.5);
          lapSplits[i] = (int32_t)(tc - lapStartT);
          ev.type = LapEventType::SplitCrossed;
          ev.splitIndex = i;
          ev.timeMs = lapSplits[i];
          const Lap* best = bestLap();
          if (best && i < best->splitMs.size() && best->splitMs[i] >= 0) {
            ev.deltaMs = lapSplits[i] - best->splitMs[i];
          }
        }
      }
    }

    double frac;
    if (moving && geo::crossesGate(geo::toLocal(prevLat, prevLon, sf.lat, sf.lon),
                                   geo::toLocal(lat, lon, sf.lat, sf.lon), sf.radius, frac)) {
      int64_t tc = prevT + (int64_t)(frac * dt + 0.5);
      uint32_t tcMillis = rxMillis - (uint32_t)(t - tc);

      if (!running) {
        startLap(tc, tcMillis);
        ev = LapEvent();
        ev.type = LapEventType::LapStarted;
      } else {
        int64_t elapsed = tc - lapStartT;
        if (elapsed >= minLap) {
          ev = LapEvent();
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
          startLap(tc, tcMillis);
        }
        // else: crossed again too soon (GPS jitter at the line) - ignore
      }
    }
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
  return ev;
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
