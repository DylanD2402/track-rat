/**
 * Track Rat - Lap Timer Implementation
 */

#include "LapTimer.h"
#include "Config.h"
#include "GPSHandler.h"

LapTimer::LapTimer() :
  currentTrack(nullptr),
  state(STATE_IDLE),
  currentLapNumber(0),
  lapStartTime(0),
  lapStartGPSTime(0),
  inFinishZone(false),
  currentLapMaxSpeed(0.0),
  currentLapSpeedSum(0.0),
  currentLapSpeedCount(0),
  bestLapIndex(-1),
  bestLapTime(0xFFFFFFFF),
  minLapTime(MIN_LAP_TIME),
  maxLapTime(MAX_LAP_TIME)
{}

void LapTimer::begin(Track* track, uint32_t minLap, uint32_t maxLap) {
  currentTrack = track;
  minLapTime = minLap;
  maxLapTime = maxLap;

  // Initialize split tracking
  if (track && !track->splits.empty()) {
    splitCrossed.resize(track->splits.size(), false);
  }

  DEBUG_PRINTF("Lap timer initialized for track: %s\n", track->name.c_str());
  DEBUG_PRINTF("Min lap time: %d ms, Max lap time: %d ms\n", minLapTime, maxLapTime);
}

void LapTimer::reset() {
  laps.clear();
  currentLapNumber = 0;
  lapStartTime = 0;
  lapStartGPSTime = 0;
  inFinishZone = false;
  bestLapIndex = -1;
  bestLapTime = 0xFFFFFFFF;
  resetLapStats();
  state = STATE_IDLE;

  DEBUG_PRINTLN("Lap timer reset");
}

void LapTimer::resetLapStats() {
  currentLapMaxSpeed = 0.0;
  currentLapSpeedSum = 0.0;
  currentLapSpeedCount = 0;
  currentSplitTimes.clear();

  if (!splitCrossed.empty()) {
    std::fill(splitCrossed.begin(), splitCrossed.end(), false);
  }
}

void LapTimer::start() {
  if (state == STATE_IDLE) {
    state = STATE_WAITING_FOR_START;
    DEBUG_PRINTLN("Lap timer started, waiting for finish line crossing");
  }
}

void LapTimer::pause() {
  if (state == STATE_RUNNING) {
    state = STATE_PAUSED;
    DEBUG_PRINTLN("Lap timer paused");
  }
}

void LapTimer::resume() {
  if (state == STATE_PAUSED) {
    state = STATE_RUNNING;
    DEBUG_PRINTLN("Lap timer resumed");
  }
}

void LapTimer::stop() {
  state = STATE_IDLE;
  DEBUG_PRINTLN("Lap timer stopped");
}

TimerState LapTimer::getState() const {
  return state;
}

void LapTimer::update(double lat, double lon, double speed, uint32_t gpsTime) {
  if (state == STATE_IDLE || state == STATE_PAUSED || !currentTrack) {
    return;
  }

  // Update current lap statistics
  if (state == STATE_RUNNING) {
    currentLapMaxSpeed = max(currentLapMaxSpeed, (float)speed);
    currentLapSpeedSum += speed;
    currentLapSpeedCount++;

    // Check split points
    for (size_t i = 0; i < currentTrack->splits.size(); i++) {
      double distanceToSplit = GPSHandler::calculateDistance(
        lat, lon,
        currentTrack->splits[i].lat,
        currentTrack->splits[i].lon
      );

      if (distanceToSplit <= currentTrack->splits[i].radius) {
        if (!splitCrossed[i]) {
          // Crossed a new split
          uint32_t splitTime = millis() - lapStartTime;
          currentSplitTimes.push_back(splitTime);
          splitCrossed[i] = true;
          DEBUG_PRINTF("Split %d crossed at %s\n", i + 1, formatTime(splitTime).c_str());
        }
      } else {
        // Reset split when we leave its zone (with hysteresis)
        if (splitCrossed[i] && distanceToSplit > currentTrack->splits[i].radius * 1.5) {
          splitCrossed[i] = false;
        }
      }
    }
  }

  // Calculate distance to start/finish line
  double distanceToFinish = GPSHandler::calculateDistance(
    lat, lon,
    currentTrack->startFinish.lat,
    currentTrack->startFinish.lon
  );

  bool currentlyInZone = (distanceToFinish <= currentTrack->startFinish.radius);

  // Detect finish line crossing (entering the zone)
  if (currentlyInZone && !inFinishZone) {
    uint32_t currentTime = millis();

    if (state == STATE_RUNNING && lapStartTime > 0) {
      // Complete current lap
      uint32_t lapTime = currentTime - lapStartTime;

      if (validateLapTime(lapTime)) {
        // Create lap data
        LapData lap;
        lap.lapNumber = currentLapNumber;
        lap.lapTime = lapTime;
        lap.timestamp = gpsTime;
        lap.maxSpeed = currentLapMaxSpeed;
        lap.avgSpeed = currentLapSpeedCount > 0 ?
                       currentLapSpeedSum / currentLapSpeedCount : 0.0;
        lap.splitTimes = currentSplitTimes;

        laps.push_back(lap);

        // Update best lap
        if (lapTime < bestLapTime) {
          bestLapTime = lapTime;
          bestLapIndex = laps.size() - 1;
          DEBUG_PRINTF("New best lap! %s\n", formatTime(lapTime).c_str());
        }

        DEBUG_PRINTF("Lap %d completed: %s\n", currentLapNumber, formatTime(lapTime).c_str());
      } else {
        DEBUG_PRINTF("Invalid lap time: %d ms\n", lapTime);
      }
    }

    // Start new lap
    lapStartTime = currentTime;
    lapStartGPSTime = gpsTime;
    currentLapNumber++;
    resetLapStats();
    state = STATE_RUNNING;

    inFinishZone = true;

    DEBUG_PRINTF("Starting lap %d\n", currentLapNumber);
  }
  else if (!currentlyInZone && inFinishZone) {
    // Exited finish zone
    inFinishZone = false;
  }
}

bool LapTimer::validateLapTime(uint32_t lapTime) {
  return (lapTime >= minLapTime && lapTime <= maxLapTime);
}

uint32_t LapTimer::getCurrentLapTime() const {
  if (state != STATE_RUNNING || lapStartTime == 0) {
    return 0;
  }
  return millis() - lapStartTime;
}

uint16_t LapTimer::getCurrentLapNumber() const {
  return currentLapNumber;
}

float LapTimer::getCurrentLapMaxSpeed() const {
  return currentLapMaxSpeed;
}

int LapTimer::getLapCount() const {
  return laps.size();
}

LapData* LapTimer::getLastLap() {
  if (laps.empty()) {
    return nullptr;
  }
  return &laps.back();
}

LapData* LapTimer::getBestLap() {
  if (bestLapIndex < 0 || bestLapIndex >= laps.size()) {
    return nullptr;
  }
  return &laps[bestLapIndex];
}

LapData* LapTimer::getLap(int index) {
  if (index < 0 || index >= laps.size()) {
    return nullptr;
  }
  return &laps[index];
}

int32_t LapTimer::getDeltaToBest() const {
  if (state != STATE_RUNNING || bestLapTime == 0xFFFFFFFF || lapStartTime == 0) {
    return 0;
  }

  uint32_t currentTime = getCurrentLapTime();
  return (int32_t)currentTime - (int32_t)bestLapTime;
}

uint32_t LapTimer::getAverageLapTime() const {
  if (laps.empty()) {
    return 0;
  }

  uint64_t totalTime = 0;
  for (const LapData& lap : laps) {
    totalTime += lap.lapTime;
  }

  return totalTime / laps.size();
}

String LapTimer::formatTime(uint32_t millis) {
  uint32_t minutes = millis / 60000;
  uint32_t seconds = (millis % 60000) / 1000;
  uint32_t ms = millis % 1000;

  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02d:%02d.%03d", minutes, seconds, ms);
  return String(buffer);
}

String LapTimer::formatDelta(int32_t deltaMillis) {
  if (deltaMillis == 0) {
    return "---";
  }

  char sign = deltaMillis > 0 ? '+' : '-';
  uint32_t absTime = abs(deltaMillis);
  uint32_t seconds = absTime / 1000;
  uint32_t ms = absTime % 1000;

  char buffer[16];
  if (seconds > 0) {
    snprintf(buffer, sizeof(buffer), "%c%d.%03d", sign, seconds, ms);
  } else {
    snprintf(buffer, sizeof(buffer), "%c0.%03d", sign, ms);
  }

  return String(buffer);
}

bool LapTimer::saveLaps(const String& filename) {
  DynamicJsonDocument doc(4096);
  JsonArray lapsArray = doc.createNestedArray("laps");

  for (const LapData& lap : laps) {
    JsonObject lapObj = lapsArray.createNestedObject();
    lapObj["lapNumber"] = lap.lapNumber;
    lapObj["lapTime"] = lap.lapTime;
    lapObj["timestamp"] = lap.timestamp;
    lapObj["maxSpeed"] = lap.maxSpeed;
    lapObj["avgSpeed"] = lap.avgSpeed;

    if (!lap.splitTimes.empty()) {
      JsonArray splitsArray = lapObj.createNestedArray("splitTimes");
      for (uint32_t splitTime : lap.splitTimes) {
        splitsArray.add(splitTime);
      }
    }
  }

  File file = LittleFS.open(filename, "w");
  if (!file) {
    DEBUG_PRINTLN("Failed to open file for writing laps");
    return false;
  }

  serializeJson(doc, file);
  file.close();

  DEBUG_PRINTF("Saved %d laps to %s\n", laps.size(), filename.c_str());
  return true;
}

bool LapTimer::loadLaps(const String& filename) {
  if (!LittleFS.exists(filename)) {
    DEBUG_PRINTLN("Lap file does not exist");
    return false;
  }

  File file = LittleFS.open(filename, "r");
  if (!file) {
    DEBUG_PRINTLN("Failed to open lap file");
    return false;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    DEBUG_PRINT("deserializeJson() failed: ");
    DEBUG_PRINTLN(error.c_str());
    return false;
  }

  laps.clear();
  bestLapTime = 0xFFFFFFFF;
  bestLapIndex = -1;

  JsonArray lapsArray = doc["laps"].as<JsonArray>();
  for (JsonObject lapObj : lapsArray) {
    LapData lap;
    lap.lapNumber = lapObj["lapNumber"];
    lap.lapTime = lapObj["lapTime"];
    lap.timestamp = lapObj["timestamp"];
    lap.maxSpeed = lapObj["maxSpeed"];
    lap.avgSpeed = lapObj["avgSpeed"];

    if (lapObj.containsKey("splitTimes")) {
      JsonArray splitsArray = lapObj["splitTimes"].as<JsonArray>();
      for (JsonVariant splitTime : splitsArray) {
        lap.splitTimes.push_back(splitTime.as<uint32_t>());
      }
    }

    laps.push_back(lap);

    // Update best lap
    if (lap.lapTime < bestLapTime) {
      bestLapTime = lap.lapTime;
      bestLapIndex = laps.size() - 1;
    }
  }

  DEBUG_PRINTF("Loaded %d laps from %s\n", laps.size(), filename.c_str());
  return true;
}
