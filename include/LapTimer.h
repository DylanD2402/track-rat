/**
 * Track Rat - Lap Timer
 *
 * Fed one GPS fix at a time. Detects start/finish and split crossings by
 * checking whether the path between consecutive fixes passes through a gate
 * (see geo::crossesGate), and interpolates the crossing time between the two
 * GPS timestamps. Lap times therefore come from GPS time, not the ESP32 clock.
 *
 * One segment between fixes can cross several lines (a split just before or
 * after the start/finish, or any lines after a gap in fixes). Every crossing
 * is applied in the order it happened along the segment, and onFix returns
 * all the resulting events, oldest first. A split before the start/finish
 * belongs to the lap that's ending, a split after it to the new lap. A split
 * at exactly the same point as the start/finish counts as before it.
 *
 * A line with a heading only counts crossings within 90 degrees of it. If the
 * start/finish heading was guessed from the map (HeadingFrom::Map) and the
 * car crosses it the "wrong" way twice in the same place before ever
 * crossing it the right way, the guess was backwards: it's turned round,
 * along with any guessed splits. (Two wrong-way crossings in different
 * places are straights alongside the line, not the line itself.)
 *
 * Every lap is tagged (pit, slow, suspect: see LapTags.h). A tagged lap is
 * never the best lap or the live-delta reference, and isn't averaged. The
 * slow and distance tags are worked out again after every lap, so an
 * earlier lap can lose its best-lap place when later laps show it was too
 * short. If the reference path's lap is tagged that way, the next clean lap
 * stands in as the reference until there's a new best.
 *
 * A lap from another day (the best ever, or the best of the day: see
 * BestLap.h) can be set as the reference with setReference, so the live
 * delta works from the first lap. Whichever is faster, it or this
 * session's reference lap, is used.
 *
 * Corner sectors (Sectors.h) are timed the same way as splits, through their
 * own gates, and kept apart from them: Lap::sectorMs, SectorCrossed events.
 *
 * No Arduino dependencies: unit-tested on a PC in test/test_laptimer.cpp.
 */

#ifndef LAP_TIMER_H
#define LAP_TIMER_H

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include "Config.h"
#include "GeoMath.h"
#include "LapTags.h"

constexpr int32_t NO_DELTA = INT32_MIN;

struct Lap {
  uint16_t number;
  uint32_t timeMs;
  float maxSpeedKmh;
  float avgSpeedKmh;
  std::vector<int32_t> splitMs;  // elapsed time at each split, -1 if missed
  std::vector<int32_t> sectorMs; // elapsed time at the end of each corner sector but the last (its gate), -1 if missed
  float distanceM = 0;           // driven, fix to fix
  uint8_t tags = 0;              // laptags:: bits
  bool clean() const { return laptags::clean(tags); }
};

enum class LapEventType { None, LapStarted, LapCompleted, SplitCrossed, SectorCrossed };

struct LapEvent {
  LapEventType type = LapEventType::None;
  uint32_t timeMs = 0;         // lap time (LapCompleted) or split time (SplitCrossed)
  int splitIndex = -1;         // the split, or for SectorCrossed the sector gate (0 = the end of T1)
  int32_t deltaMs = NO_DELTA;  // vs best lap (or best lap's split)
  bool isBest = false;
};

// The events from one fix, oldest first. A fixed-size list: onFix runs 20
// times a second and shouldn't allocate.
struct LapEvents {
  static constexpr size_t CAPACITY = 8;  // if a segment ever crosses more, the newest replaces the last
  LapEvent items[CAPACITY];
  size_t count = 0;

  void add(const LapEvent& e) { items[count < CAPACITY ? count++ : CAPACITY - 1] = e; }
  bool empty() const { return count == 0; }
  const LapEvent* begin() const { return items; }
  const LapEvent* end() const { return items + count; }
  // The newest event, or an empty one (type None) if there were none.
  LapEvent last() const { return count ? items[count - 1] : LapEvent(); }
  // The first event of this type, or nullptr.
  const LapEvent* find(LapEventType type) const {
    for (const LapEvent& e : *this)
      if (e.type == type) return &e;
    return nullptr;
  }
};

class LapTimer {
public:
  // Start/finish crossings sooner than minLapMs after the last are ignored (GPS jitter).
  void begin(const TrackPoint& startFinish, const std::vector<TrackPoint>& splits,
             uint32_t minLapMs = MIN_LAP_TIME_MS, const std::vector<TrackPoint>& sectorGates = {});
  void reset();

  // Swap in another layout's lines without losing the laps so far (used
  // when the layout changes and its start/finish is in the same place).
  void changeGates(const TrackPoint& startFinish, const std::vector<TrackPoint>& splits);
  const TrackPoint& startFinish() const { return sf; }
  const std::vector<TrackPoint>& splits() const { return splitPts; }
  // Corner sector gates, in order from the line (made after the first clean
  // lap: see Sectors.h). Laps from now on are timed through them.
  void setSectors(const std::vector<TrackPoint>& gates);
  const std::vector<TrackPoint>& sectorGates() const { return sectorPts; }

  // rxMillis is the device clock when the fix arrived; only used to run the
  // live on-screen clock between fixes. hAccM: the fix's horizontal
  // accuracy (+/- m), or < 0 if the GPS doesn't say.
  LapEvents onFix(double lat, double lon, float speedKmh, uint32_t gpsTimeMs, uint32_t rxMillis, float hAccM = -1);

  // Start lap 1 now (the driver just marked the start/finish line here).
  void startLapNow(uint32_t gpsTimeMs, uint32_t rxMillis);

  bool lapRunning() const { return running; }
  uint16_t currentLapNumber() const { return lapNumber; }
  uint32_t currentLapMs(uint32_t nowMillis) const;

  size_t lapCount() const { return laps.size(); }
  const Lap& lap(size_t i) const { return laps[i]; }
  const Lap* lastLap() const { return laps.empty() ? nullptr : &laps.back(); }
  const Lap* bestLap() const { return bestIndex < 0 ? nullptr : &laps[bestIndex]; }
  int bestLapIndex() const { return bestIndex; }  // the best clean lap
  uint32_t averageLapMs() const;  // of the untagged laps; 0 if there are none
  size_t untaggedLapCount() const;
  uint8_t currentLapTags() const { return running ? lapTags : 0; }  // so far (pit and suspect only)
  float sessionMaxSpeedKmh() const { return sessionMaxSpeed; }

  // Live gap to the reference lap at the same point on track (+ = slower).
  // NO_DELTA before there is one, or when off its line.
  int32_t liveDeltaMs() const { return liveDelta; }

  // A point on a lap's path: metres from the start/finish, ms into the lap.
  struct TracePt {
    float x, y;
    int32_t t;
    float speedKmh;
  };
  // Path driven on the best lap, and so far on the current lap (for the map
  // screen when a track has no stored layout).
  const std::vector<TracePt>& bestPath() const { return bestTrace; }
  int bestPathLapIndex() const { return refIndex; }  // the lap bestPath() is from (-1: none)
  const std::vector<TracePt>& currentPath() const { return curTrace; }
  // The path the live delta is measured against (a stored lap or this
  // session's), empty if none: the map's ghost dot.
  const std::vector<TracePt>& referencePath() const { return activeTrace(); }
  // The lap under way's elapsed time at each corner sector gate so far (-1:
  // not yet, or missed), for the sector strip. Empty without a lap or sectors.
  const std::vector<int32_t>& currentSectorMs() const { return running ? lapSectors : noSectors; }

  // A stored lap to compare with (its path ends at the line, at its lap
  // time). Used while it's faster than this session's reference lap.
  // Cleared by begin() and reset().
  void setReference(std::vector<TracePt> path, uint32_t lapMs);
  void clearReference();

  // For the debrief (Debrief.h): the path through each corner sector of the
  // clean lap with the best time there, and that time (-1 / empty if none).
  // "Clean" when the lap was completed: a lap tagged slow later still counts
  // here, so check the time against the debrief's before using the path.
  const std::vector<TracePt>& sectorBestPath(size_t sector) const;
  int32_t sectorBestMs(size_t sector) const { return sector < sectorBest.size() ? sectorBest[sector] : -1; }
  bool usingStoredReference() const;
  // The lap time of the reference the live delta is against, 0 if none.
  uint32_t referenceLapMs() const;

private:
  std::vector<TracePt> curTrace;
  std::vector<TracePt> bestTrace;
  int refIndex = -1;  // the lap bestTrace is from
  std::vector<TracePt> storedTrace;  // setReference
  std::vector<std::vector<TracePt>> sectorBestTrace;  // per corner sector
  std::vector<int32_t> sectorBest;
  void keepSectorBests(const Lap& lap);
  uint32_t storedMs = 0;
  bool matchedStored = false;  // the last match was against storedTrace
  const std::vector<TracePt>& activeTrace() const;
  size_t matchIdx = 0;
  int32_t liveDelta = NO_DELTA;

  TrackPoint sf = {0, 0, 0};
  std::vector<TrackPoint> splitPts;
  std::vector<TrackPoint> sectorPts;
  bool sfHeadingConfirmed = false;  // crossed the right way at least once
  bool sfWrongWay = false;          // crossed the wrong way since the last right-way crossing...
  geo::XY sfWrongAt = {0, 0};       // ...here, in metres from the start/finish
  std::vector<Lap> laps;
  int bestIndex = -1;
  uint32_t minLap = MIN_LAP_TIME_MS;

  // previous fix
  bool havePrev = false;
  double prevLat = 0, prevLon = 0;
  int64_t prevT = 0;
  float prevSpeed = 0;

  // GPS time-of-day -> continuous time across midnight
  bool haveTimeBase = false;
  uint32_t lastRawGps = 0;
  int64_t dayOffset = 0;

  // current lap
  bool running = false;
  uint16_t lapNumber = 0;
  int64_t lapStartT = 0;
  uint32_t lapStartMillis = 0;
  float lapMaxSpeed = 0;
  double lapSpeedSum = 0;
  uint32_t lapSpeedCount = 0;
  std::vector<int32_t> lapSplits;
  std::vector<int32_t> lapSectors;
  std::vector<int32_t> noSectors;  // stays empty
  float lapDistance = 0;
  uint8_t lapTags = 0;

  float sessionMaxSpeed = 0;

  int64_t continuousTime(uint32_t gpsTimeMs);
  bool startFinishCounts(geo::XY a, geo::XY b, double frac);
  void crossSplits(double lat, double lon, int64_t dt, double after, double upTo, LapEvents& out);
  void crossGates(bool sectorGates, double lat, double lon, int64_t dt, double after, double upTo, LapEvents& out);
  void startLap(int64_t t, uint32_t startMillis);
  int32_t matchBestTrace(float x, float y, int32_t elapsed);
  void retagLaps();
};

#endif  // LAP_TIMER_H
