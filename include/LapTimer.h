/**
 * Track Rat - Lap Timer
 *
 * Fed one GPS fix at a time. Detects start/finish and split crossings by
 * checking whether the path between consecutive fixes passes through a gate
 * (see geo::crossesGate), and interpolates the crossing time between the two
 * GPS timestamps. Lap times therefore come from GPS time, not the ESP32 clock.
 *
 * No Arduino dependencies: unit-tested on a PC in test/test_laptimer.cpp.
 */

#ifndef LAP_TIMER_H
#define LAP_TIMER_H

#include <stdint.h>
#include <stddef.h>
#include <vector>
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
  void begin(const TrackPoint& startFinish, const std::vector<TrackPoint>& splits);
  void reset();

  // rxMillis is the device clock when the fix arrived; only used to run the
  // live on-screen clock between fixes.
  LapEvent onFix(double lat, double lon, float speedKmh, uint32_t gpsTimeMs, uint32_t rxMillis);

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

private:
  TrackPoint sf = {0, 0, 0};
  std::vector<TrackPoint> splitPts;
  std::vector<Lap> laps;
  int bestIndex = -1;

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
  void startLap(int64_t t, uint32_t startMillis);
};

#endif  // LAP_TIMER_H
