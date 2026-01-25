/**
 * Track Rat - Lap Timer
 * Handles lap detection and timing
 */

#ifndef LAP_TIMER_H
#define LAP_TIMER_H

#include <Arduino.h>
#include <vector>
#include "TrackDatabase.h"

struct LapData {
  uint16_t lapNumber;
  uint32_t lapTime;  // milliseconds
  uint32_t timestamp;  // GPS time when lap completed
  float maxSpeed;  // km/h
  float avgSpeed;  // km/h
  std::vector<uint32_t> splitTimes;  // milliseconds

  LapData() : lapNumber(0), lapTime(0), timestamp(0), maxSpeed(0.0), avgSpeed(0.0) {}
};

enum TimerState {
  STATE_IDLE,
  STATE_WAITING_FOR_START,
  STATE_RUNNING,
  STATE_PAUSED
};

class LapTimer {
private:
  Track* currentTrack;
  TimerState state;

  // Lap tracking
  std::vector<LapData> laps;
  uint16_t currentLapNumber;
  uint32_t lapStartTime;  // millis()
  uint32_t lapStartGPSTime;  // GPS time
  bool inFinishZone;

  // Split tracking
  std::vector<bool> splitCrossed;
  std::vector<uint32_t> currentSplitTimes;

  // Current lap statistics
  float currentLapMaxSpeed;
  float currentLapSpeedSum;
  uint32_t currentLapSpeedCount;

  // Best lap
  int bestLapIndex;
  uint32_t bestLapTime;

  // Validation
  uint32_t minLapTime;
  uint32_t maxLapTime;

  void resetLapStats();
  bool validateLapTime(uint32_t lapTime);

public:
  LapTimer();

  void begin(Track* track, uint32_t minLap = 30000, uint32_t maxLap = 600000);
  void reset();

  // State management
  void start();
  void pause();
  void resume();
  void stop();
  TimerState getState() const;

  // Update
  void update(double lat, double lon, double speed, uint32_t gpsTime);

  // Current lap info
  uint32_t getCurrentLapTime() const;
  uint16_t getCurrentLapNumber() const;
  float getCurrentLapMaxSpeed() const;

  // Lap data
  int getLapCount() const;
  LapData* getLastLap();
  LapData* getBestLap();
  LapData* getLap(int index);

  // Timing comparisons
  int32_t getDeltaToBest() const;  // milliseconds (+ slower, - faster)
  uint32_t getAverageLapTime() const;

  // Format helpers
  static String formatTime(uint32_t millis);
  static String formatDelta(int32_t deltaMillis);

  // Data export
  bool saveLaps(const String& filename);
  bool loadLaps(const String& filename);
};

#endif // LAP_TIMER_H
