/**
 * Track Rat - Lap Timer Implementation
 */

#include "LapTimer.h"
#include "Config.h"
#include "Debrief.h"

void LapTimer::begin(const TrackPoint& startFinish, const std::vector<TrackPoint>& splits, uint32_t minLapMs,
                     const std::vector<TrackPoint>& sectorGates) {
  stage = finHave = false;  // a circuit, unless setStage says otherwise
  sf = startFinish;
  minLap = minLapMs;
  splitPts = splits;
  sectorPts = sectorGates;
  reset();
}

void LapTimer::setStage(const TrackPoint& finish, bool hasFinish) {
  stage = true;
  fin = finish;
  finHave = hasFinish;
  if (minLap == MIN_LAP_TIME_MS) minLap = STAGE_MIN_RUN_MS;  // a run can be short; the finish is elsewhere anyway
}

void LapTimer::changeGates(const TrackPoint& startFinish, const std::vector<TrackPoint>& splits) {
  sf = startFinish;
  splitPts = splits;
  sfHeadingConfirmed = false;
  sfWrongWay = false;
  if (running) lapSplits.assign(splitPts.size(), -1);
}

void LapTimer::setSectors(const std::vector<TrackPoint>& gates) {
  sectorPts = gates;
  sectorBestTrace.clear();
  sectorBest.clear();
  lapSectors.assign(sectorPts.size(), -1);  // the lap under way is timed through them from here
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
  sessionCoolantMax = sessionOilMax = lapCoolantMax = lapOilMax = NO_TEMP;
  curTrace.clear();
  bestTrace.clear();
  refIndex = -1;
  storedTrace.clear();
  storedMs = 0;
  sectorBestTrace.clear();
  sectorBest.clear();
  matchIdx = 0;
  liveDelta = NO_DELTA;
  sfHeadingConfirmed = false;
  sfWrongWay = false;
  armedHere = armedGo = stopping = false;
  undoable = false;
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
  undoable = false;
  running = true;
  lapNumber++;
  lapStartT = t;
  lapStartMillis = startMillis;
  lapMaxSpeed = 0;
  lapCoolantMax = lapOilMax = NO_TEMP;
  lapSpeedSum = 0;
  lapSpeedCount = 0;
  lapSplits.assign(splitPts.size(), -1);
  lapSectors.assign(sectorPts.size(), -1);
  lapDistance = 0;
  lapTags = 0;
  curTrace.clear();
  curTrace.reserve(2048);
  curTrace.push_back({0, 0, 0, 0});  // the start/finish is the origin
  matchIdx = 0;
  liveDelta = NO_DELTA;
  stopping = false;
}

// The lap (or run) under way ends at tc, at endAt (metres from the start/finish).
// False if it isn't kept (too long, or too many laps): ev is then a LapStarted.
bool LapTimer::completeLap(int64_t tc, geo::XY endAt, float speedKmh, LapEvent& ev) {
  const int64_t elapsed = tc - lapStartT;
  if (elapsed > MAX_LAP_TIME_MS || laps.size() >= MAX_LAPS_STORED) {
    ev.type = LapEventType::LapStarted;  // too long (pit stop etc.) - not recorded
    return false;
  }
  Lap lap;
  lap.number = lapNumber;
  lap.timeMs = (uint32_t)elapsed;
  lap.maxSpeedKmh = lapMaxSpeed;
  lap.avgSpeedKmh = lapSpeedCount ? (float)(lapSpeedSum / lapSpeedCount) : 0;
  lap.splitMs = lapSplits;
  lap.sectorMs = lapSectors;
  lap.distanceM = lapDistance;
  lap.tags = lapTags;
  lap.coolantMaxC = lapCoolantMax;
  lap.oilMaxC = lapOilMax;

  const Lap* prevBest = bestLap();
  if (prevBest) ev.deltaMs = (int32_t)lap.timeMs - (int32_t)prevBest->timeMs;
  laps.push_back(lap);
  retagLaps();
  const int idx = (int)laps.size() - 1;
  ev.isBest = bestIndex == idx;
  keepSectorBests(laps[idx]);  // before its path is handed on
  // A reference whose lap turned out tagged is dropped; this lap's path becomes the
  // reference if it's the new best, or stands in while there's none.
  if (refIndex >= 0 && !laps[refIndex].clean()) {
    bestTrace.clear();
    refIndex = -1;
  }
  if (ev.isBest || (refIndex < 0 && laps[idx].tags == 0)) {  // a stand-in has no tags at all, not even slow
    curTrace.push_back({(float)endAt.x, (float)endAt.y, (int32_t)lap.timeMs, speedKmh});
    bestTrace.swap(curTrace);
    refIndex = idx;
  }
  ev.type = LapEventType::LapCompleted;
  ev.timeMs = lap.timeMs;
  return true;
}

LapEvents LapTimer::finishRunNow(double lat, double lon, uint32_t gpsTimeMs) {
  LapEvents evs;
  if (!stage || !running) return evs;
  const int64_t t = continuousTime(gpsTimeMs);
  LapEvent ev;
  if (t - lapStartT > 0 && completeLap(t, geo::toLocal(lat, lon, sf.lat, sf.lon), prevSpeed, ev)) {
    evs.add(ev);
    undoable = laps.size() == 1;  // (the first run: there was no finish, so no run before it either)
  }
  running = false;
  return evs;
}

bool LapTimer::undoFinishRun() {
  if (!stage || running || !undoable || laps.size() != 1) return false;
  undoable = false;
  if (refIndex == 0) {  // this run's path was kept as the best: it is the run under way again
    curTrace.swap(bestTrace);
    if (!curTrace.empty()) curTrace.pop_back();  // (the point at the finish)
  }
  bestTrace.clear();
  refIndex = -1;
  laps.clear();
  bestIndex = -1;
  sectorBest.clear();
  sectorBestTrace.clear();
  running = true;
  stopping = false;
  liveDelta = NO_DELTA;
  matchIdx = 0;
  return true;
}

// Records the splits crossed on the segment from the previous fix to (lat, lon), whose crossing
// fraction is above `after` and at most `upTo`, in the order they were crossed. Called once for the
// part of the segment before the start/finish and once for the part after it.
void LapTimer::crossSplits(double lat, double lon, int64_t dt, double after, double upTo, LapEvents& out) {
  crossGates(false, lat, lon, dt, after, upTo, out);
  crossGates(true, lat, lon, dt, after, upTo, out);
}

// The same for one set of gates: the splits, or the corner sectors' gates.
void LapTimer::crossGates(bool sectorGates, double lat, double lon, int64_t dt, double after, double upTo,
                          LapEvents& out) {
  const std::vector<TrackPoint>& gates = sectorGates ? sectorPts : splitPts;
  std::vector<int32_t>& times = sectorGates ? lapSectors : lapSplits;
  for (;;) {
    int next = -1;
    double nextFrac = 0;
    for (size_t i = 0; i < gates.size() && i < times.size(); i++) {
      if (times[i] >= 0) continue;  // already crossed this lap
      const TrackPoint& sp = gates[i];
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
    times[next] = (int32_t)(tc - lapStartT);
    LapEvent ev;
    ev.type = sectorGates ? LapEventType::SectorCrossed : LapEventType::SplitCrossed;
    ev.splitIndex = next;
    ev.timeMs = times[next];
    const Lap* best = bestLap();
    const std::vector<int32_t>* bestTimes = best ? (sectorGates ? &best->sectorMs : &best->splitMs) : nullptr;
    if (bestTimes && (size_t)next < bestTimes->size() && (*bestTimes)[next] >= 0) {
      ev.deltaMs = times[next] - (*bestTimes)[next];
    }
    out.add(ev);
  }
}

LapEvents LapTimer::onFix(double lat, double lon, float speedKmh, uint32_t gpsTimeMs, uint32_t rxMillis, float hAccM) {
  LapEvents evs;
  int64_t t = continuousTime(gpsTimeMs);

  if (speedKmh > sessionMaxSpeed) sessionMaxSpeed = speedKmh;

  int64_t dt = t - prevT;
  // A gap before this fix: suspect, for the lap it happened in (and any lap starting at this fix)
  const uint8_t gapFlag = havePrev && dt > 0 ? laptags::gapFlag(dt > UINT32_MAX ? UINT32_MAX : (uint32_t)dt) : 0;
  if (running) lapTags |= gapFlag;
  bool usable = havePrev && dt > 0 && dt <= MAX_FIX_GAP_MS;

  // Point to point: stopped at the start arms the run; the first movement then starts the clock
  if (stage && !running) {
    if (speedKmh < STAGE_STILL_KMH) stillT = t;
    const geo::XY p = geo::toLocal(lat, lon, sf.lat, sf.lon);
    const double d = hypot(p.x, p.y);
    if (d > STAGE_ARM_M) {
      armedHere = armedGo = false;
    } else if (speedKmh < STAGE_STOPPED_KMH) {
      armedHere = true;
      // Behind a start line with a direction (metres along it < 0): the line starts the clock
      const double along = sf.hasHeading() ? p.x * sin(sf.heading * geo::DEG2RAD) + p.y * cos(sf.heading * geo::DEG2RAD) : 0;
      armedGo = along >= -STAGE_LIGHTS_M;
    } else if (armedGo && speedKmh >= STAGE_GO_KMH && usable) {
      // From the last fix standing still (it takes a few fixes to reach STAGE_GO_KMH)
      const int64_t t0 = t - stillT <= 3000 ? stillT : prevT;
      startLap(t0, rxMillis - (uint32_t)(t - t0));
      armedHere = armedGo = false;
      LapEvent ev;
      ev.type = LapEventType::LapStarted;
      evs.add(ev);
    }
  }

  if (usable) {
    bool moving = (speedKmh >= MIN_CROSSING_SPEED_KMH || prevSpeed >= MIN_CROSSING_SPEED_KMH);

    // A segment can cross splits and the start/finish together, so find where the line is first:
    // splits up to it belong to the lap that's ending (with the directions as they were),
    // splits after it to the lap that starts (with the directions as the line left them).
    geo::XY a = geo::toLocal(prevLat, prevLon, sf.lat, sf.lon);
    geo::XY b = geo::toLocal(lat, lon, sf.lat, sf.lon);
    double frac = 0;
    bool lineCrossed = moving && geo::crossesGate(a, b, sf.radius, frac);
    // Point to point: the start only starts a run; the finish ends it
    bool finCrossed = false;
    double finFrac = 0;
    if (stage) {
      // A start line with no direction (NEW COURSE: put down where the car stood) only
      // starts standing starts, or every drive back to the start would start a run
      if (running || !sf.hasHeading()) lineCrossed = false;
      if (running && finHave && moving) {
        geo::XY fa = geo::toLocal(prevLat, prevLon, fin.lat, fin.lon);
        geo::XY fb = geo::toLocal(lat, lon, fin.lat, fin.lon);
        finCrossed = geo::crossesGate(fa, fb, fin.radius, finFrac) &&
                     (!fin.hasHeading() || geo::travelsWith(fa, fb, fin.heading)) &&
                     prevT + (int64_t)(finFrac * dt + 0.5) - lapStartT >= minLap;
      }
    }

    if (running) {
      if (speedKmh > lapMaxSpeed) lapMaxSpeed = speedKmh;
      lapSpeedSum += speedKmh;
      lapSpeedCount++;
      if (moving) lapDistance += (float)hypot(b.x - a.x, b.y - a.y);  // not GPS wander while parked; the segment that ends a lap counts to it
      // A split at the very same point as the line counts as before it: it was checked first before this too.
      if (moving) crossSplits(lat, lon, dt, -1.0, lineCrossed ? frac : finCrossed ? finFrac : 2.0, evs);
    }

    if (finCrossed) {  // the end of a run: no lap starts
      const int64_t tc = prevT + (int64_t)(finFrac * dt + 0.5);
      geo::XY at = geo::toLocal(prevLat + finFrac * (lat - prevLat), prevLon + finFrac * (lon - prevLon), sf.lat, sf.lon);
      LapEvent ev;
      if (completeLap(tc, at, speedKmh, ev)) evs.add(ev);
      running = false;
    } else if (lineCrossed && startFinishCounts(a, b, frac)) {
      int64_t tc = prevT + (int64_t)(frac * dt + 0.5);
      uint32_t tcMillis = rxMillis - (uint32_t)(t - tc);

      if (!running) {
        startLap(tc, tcMillis);
        armedHere = armedGo = false;
        LapEvent ev;
        ev.type = LapEventType::LapStarted;
        evs.add(ev);
      } else {
        int64_t elapsed = tc - lapStartT;
        if (elapsed >= minLap) {
          LapEvent ev;
          completeLap(tc, {0, 0}, speedKmh, ev);
          evs.add(ev);
          startLap(tc, tcMillis);
        }
        // else: crossed again too soon (GPS jitter at the line) - ignore
      }
    }

    // The rest of the segment, in whichever lap is running now (the same one if the line didn't count).
    if (lineCrossed && running && moving) crossSplits(lat, lon, dt, frac, 2.0, evs);
  }

  // Point to point: a run that stops before the finish (or with none set) ends, untimed
  if (stage && running) {
    if (speedKmh >= STAGE_STOPPED_KMH) {
      stopping = false;
    } else if (!stopping) {
      stopping = true;
      stopSinceT = t;
    }
    if ((stopping && t - stopSinceT >= STAGE_END_STOP_MS) || t - lapStartT > MAX_LAP_TIME_MS) {
      running = false;
      stopping = false;
      LapEvent ev;
      ev.type = LapEventType::RunEnded;
      evs.add(ev);
    }
  }

  if (running) {
    lapTags |= gapFlag | laptags::fixFlags(speedKmh, hAccM, 0);  // stopped, or a poor fix, in the lap running now
    int32_t elapsed = (int32_t)(t - lapStartT);
    geo::XY p = geo::toLocal(lat, lon, sf.lat, sf.lon);
    if (curTrace.size() < MAX_TRACE_POINTS) curTrace.push_back({(float)p.x, (float)p.y, elapsed, speedKmh});
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
  uint64_t total = 0;
  size_t n = 0;
  for (const Lap& l : laps)
    if (!l.tags) total += l.timeMs, n++;
  return n ? total / n : 0;
}

size_t LapTimer::untaggedLapCount() const {
  size_t n = 0;
  for (const Lap& l : laps) n += l.tags == 0;
  return n;
}

// The slow and distance tags, and so the best lap, from all the laps so far (LapTags.h).
void LapTimer::retagLaps() {
  std::vector<laptags::LapInfo> info;
  info.reserve(laps.size());
  for (const Lap& l : laps) info.push_back({l.timeMs, l.distanceM, l.tags});
  bestIndex = laptags::retag(info);
  for (size_t i = 0; i < laps.size(); i++) laps[i].tags = info[i].tags;
}

// Finds where on the best lap's path we are now, and how long the best lap
// took to get there. Searches a window around the last match so the start
// and end of the lap (both near the start/finish) don't get confused.
// A clean lap with a sector time better than any so far: keep its path through that sector.
void LapTimer::keepSectorBests(const Lap& lap) {
  const size_t n = sectorPts.size() + 1;
  std::vector<int32_t> times = debrief::sectorTimes(lap.timeMs, lap.sectorMs, n);
  if (lap.tags || times.empty()) return;
  if (sectorBest.size() != n) {
    sectorBest.assign(n, -1);
    sectorBestTrace.assign(n, std::vector<TracePt>());
  }
  int32_t from = 0;
  for (size_t s = 0; s < n; s++) {
    const int32_t to = from + times[s];
    if (sectorBest[s] < 0 || times[s] < sectorBest[s]) {
      sectorBest[s] = times[s];
      std::vector<TracePt>& out = sectorBestTrace[s];
      out.clear();
      for (const TracePt& p : curTrace)
        if (p.t >= from && p.t <= to) out.push_back(p);
    }
    from = to;
  }
}

const std::vector<LapTimer::TracePt>& LapTimer::sectorBestPath(size_t sector) const {
  static const std::vector<TracePt> none;
  return sector < sectorBestTrace.size() ? sectorBestTrace[sector] : none;
}

void LapTimer::setReference(std::vector<TracePt> path, uint32_t lapMs) {
  if (path.size() < 2 || lapMs == 0) return clearReference();
  storedTrace = std::move(path);
  storedMs = lapMs;
}

void LapTimer::clearReference() {
  std::vector<TracePt>().swap(storedTrace);
  storedMs = 0;
}

bool LapTimer::usingStoredReference() const {
  return !storedTrace.empty() && (bestTrace.empty() || storedMs < (uint32_t)bestTrace.back().t);
}

const std::vector<LapTimer::TracePt>& LapTimer::activeTrace() const {
  return usingStoredReference() ? storedTrace : bestTrace;
}

uint32_t LapTimer::referenceLapMs() const {
  if (usingStoredReference()) return storedMs;
  return bestTrace.size() >= 2 ? (uint32_t)bestTrace.back().t : 0;
}

int32_t LapTimer::matchBestTrace(float x, float y, int32_t elapsed) {
  const std::vector<TracePt>& bestTrace = activeTrace();  // (shadows the member: the reference in use)
  if (usingStoredReference() != matchedStored) {  // another reference: search it from the start
    matchedStored = !matchedStored;
    matchIdx = 0;
  }
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
