/**
 * Track Rat - Lap Timer
 *
 * Fed one GPS fix at a time. Detects start/finish and split crossings by
 * checking whether the path between consecutive fixes passes through a gate
 * (see geo::crossesGate), and interpolates the crossing time between the two
 * GPS timestamps. Lap times therefore come from GPS time, not the ESP32 clock.
 *
 * A line with a heading only counts crossings within 90 degrees of it. If the
 * start/finish heading was guessed from the map (HeadingFrom::Map) and the
 * car crosses it the "wrong" way twice in the same place before ever
 * crossing it the right way, the guess was backwards: it's turned round,
 * along with any guessed splits. (Two wrong-way crossings in different
 * places are straights alongside the line, not the line itself.)
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

constexpr int32_t NO_DELTA = INT32_MIN;

struct Lap {
  uint16_t number;
  uint32_t timeMs;
  float maxSpeedKmh;
  float avgSpeedKmh;
  std::vector<int32_t> splitMs;  // elapsed time at each split, -1 if missed
};

enum class LapEventType { None, LapStarted, LapCompleted, SplitCrossed };

struct LapEvent {
  LapEventType type = LapEventType::None;
  uint32_t timeMs = 0;         // lap time (LapCompleted) or split time (SplitCrossed)
  int splitIndex = -1;
  int32_t deltaMs = NO_DELTA;  // vs best lap (or best lap's split)
  bool isBest = false;
};

class LapTimer {
public:
  // Start/finish crossings sooner than minLapMs after the last are ignored (GPS jitter).
  void begin(const TrackPoint& startFinish, const std::vector<TrackPoint>& splits,
             uint32_t minLapMs = MIN_LAP_TIME_MS);
  void reset();

  // Swap in another layout's lines without losing the laps so far (used
  // when the layout changes and its start/finish is in the same place).
  void changeGates(const TrackPoint& startFinish, const std::vector<TrackPoint>& splits);
  const TrackPoint& startFinish() const { return sf; }

  // rxMillis is the device clock when the fix arrived; only used to run the
  // live on-screen clock between fixes.
  LapEvent onFix(double lat, double lon, float speedKmh, uint32_t gpsTimeMs, uint32_t rxMillis);

  // Start lap 1 now (the driver just marked the start/finish line here).
  void startLapNow(uint32_t gpsTimeMs, uint32_t rxMillis);

  bool lapRunning() const { return running; }
  uint16_t currentLapNumber() const { return lapNumber; }
  uint32_t currentLapMs(uint32_t nowMillis) const;

  size_t lapCount() const { return laps.size(); }
  const Lap& lap(size_t i) const { return laps[i]; }
  const Lap* lastLap() const { return laps.empty() ? nullptr : &laps.back(); }
  const Lap* bestLap() const { return bestIndex < 0 ? nullptr : &laps[bestIndex]; }
  int bestLapIndex() const { return bestIndex; }
  uint32_t averageLapMs() const;
  float sessionMaxSpeedKmh() const { return sessionMaxSpeed; }

  // Live gap to the best lap at the same point on track (+ = slower).
  // NO_DELTA before there is a best lap, or when off the best lap's line.
  int32_t liveDeltaMs() const { return liveDelta; }

  // A point on a lap's path: metres from the start/finish, ms into the lap.
  struct TracePt {
    float x, y;
    int32_t t;
  };
  // Path driven on the best lap, and so far on the current lap (for the map
  // screen when a track has no stored layout).
  const std::vector<TracePt>& bestPath() const { return bestTrace; }
  const std::vector<TracePt>& currentPath() const { return curTrace; }

private:
  std::vector<TracePt> curTrace;
  std::vector<TracePt> bestTrace;
  size_t matchIdx = 0;
  int32_t liveDelta = NO_DELTA;

  TrackPoint sf = {0, 0, 0};
  std::vector<TrackPoint> splitPts;
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

  float sessionMaxSpeed = 0;

  int64_t continuousTime(uint32_t gpsTimeMs);
  bool startFinishCounts(geo::XY a, geo::XY b, double frac);
  void startLap(int64_t t, uint32_t startMillis);
  int32_t matchBestTrace(float x, float y, int32_t elapsed);
};

#endif  // LAP_TIMER_H
