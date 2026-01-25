/**
 * Track Rat - Display Manager Implementation
 */

#include "DisplayManager.h"
#include "Config.h"

DisplayManager::DisplayManager() :
  display(nullptr),
  currentMode(MODE_BOOT),
  lastUpdate(0),
  updateInterval(DISPLAY_UPDATE_INTERVAL),
  trackDB(nullptr),
  lapTimer(nullptr),
  gpsHandler(nullptr),
  selectedTrackIndex(0)
{}

DisplayManager::~DisplayManager() {
  if (display) {
    delete display;
  }
}

bool DisplayManager::begin(uint8_t sda, uint8_t scl, uint8_t address) {
  Wire.begin(sda, scl);

  display = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

  if (!display->begin(SSD1306_SWITCHCAPVCC, address)) {
    DEBUG_PRINTLN("SSD1306 allocation failed");
    return false;
  }

  display->clearDisplay();
  display->setTextColor(SSD1306_WHITE);
  display->setTextSize(1);
  display->display();

  DEBUG_PRINTLN("Display initialized");
  return true;
}

void DisplayManager::setComponents(TrackDatabase* db, LapTimer* timer, GPSHandler* gps) {
  trackDB = db;
  lapTimer = timer;
  gpsHandler = gps;
}

void DisplayManager::setMode(DisplayMode mode) {
  if (currentMode != mode) {
    currentMode = mode;
    lastUpdate = 0;  // Force immediate update
    DEBUG_PRINTF("Display mode changed to: %d\n", mode);
  }
}

DisplayMode DisplayManager::getMode() const {
  return currentMode;
}

void DisplayManager::nextTrack() {
  if (trackDB) {
    selectedTrackIndex = (selectedTrackIndex + 1) % trackDB->getTrackCount();
    DEBUG_PRINTF("Selected track index: %d\n", selectedTrackIndex);
  }
}

void DisplayManager::previousTrack() {
  if (trackDB) {
    selectedTrackIndex--;
    if (selectedTrackIndex < 0) {
      selectedTrackIndex = trackDB->getTrackCount() - 1;
    }
    DEBUG_PRINTF("Selected track index: %d\n", selectedTrackIndex);
  }
}

int DisplayManager::getSelectedTrackIndex() const {
  return selectedTrackIndex;
}

void DisplayManager::update() {
  if (!display) return;

  unsigned long now = millis();
  if (now - lastUpdate < updateInterval) {
    return;
  }

  lastUpdate = now;

  switch (currentMode) {
    case MODE_BOOT:
      showBootScreen();
      break;
    case MODE_GPS_WAITING:
      showGPSWaitScreen();
      break;
    case MODE_TRACK_SELECTION:
      showTrackSelectionScreen();
      break;
    case MODE_TRACK_CONFIRM:
      showTrackConfirmScreen();
      break;
    case MODE_LAP_TIMING:
      showLapTimingScreen();
      break;
    case MODE_SESSION_SUMMARY:
      showSessionSummaryScreen();
      break;
  }

  display->display();
}

void DisplayManager::showBootScreen() {
  clear();
  drawHeader("TRACK RAT");
  display->setCursor(10, 24);
  display->println("GPS Lap Timer");
  display->setCursor(10, 40);
  display->println("Initializing...");
}

void DisplayManager::showGPSWaitScreen() {
  clear();
  drawHeader("GPS STATUS");

  if (gpsHandler) {
    uint8_t sats = gpsHandler->getSatellites();
    bool hasFix = gpsHandler->hasFix();

    display->setCursor(0, 16);
    display->printf("Satellites: %d", sats);

    display->setCursor(0, 28);
    if (hasFix) {
      display->println("Status: FIX OK");
    } else {
      display->println("Status: NO FIX");

      // Animated waiting indicator
      uint8_t dots = (millis() / 500) % 4;
      display->setCursor(0, 40);
      display->print("Waiting");
      for (uint8_t i = 0; i < dots; i++) {
        display->print(".");
      }
    }

    // Progress bar
    uint8_t progress = min(sats * 100 / 8, 100);  // 8 sats = 100%
    drawProgressBar(0, 56, SCREEN_WIDTH, 8, progress);
  }
}

void DisplayManager::showTrackSelectionScreen() {
  clear();
  drawHeader("SELECT TRACK");

  if (trackDB && trackDB->getTrackCount() > 0) {
    Track* track = trackDB->getTrack(selectedTrackIndex);
    if (track) {
      // Track name (centered, large)
      display->setTextSize(1);
      display->setCursor(0, 20);
      drawCentered(track->name, 20);

      // Location
      display->setCursor(0, 32);
      drawCentered(track->location, 32);

      // Track info
      display->setCursor(0, 44);
      char info[32];
      snprintf(info, sizeof(info), "%.2f km", track->length / 1000.0);
      drawCentered(String(info), 44);

      // Navigation hint
      display->setCursor(0, 56);
      char nav[32];
      snprintf(nav, sizeof(nav), "<%d/%d>", selectedTrackIndex + 1, trackDB->getTrackCount());
      drawCentered(String(nav), 56);
    }
  } else {
    display->setCursor(0, 28);
    drawCentered("No tracks found", 28);
  }
}

void DisplayManager::showTrackConfirmScreen() {
  clear();
  drawHeader("CONFIRM");

  if (trackDB) {
    Track* track = trackDB->getTrack(selectedTrackIndex);
    if (track) {
      display->setCursor(0, 20);
      drawCentered(track->name, 20);

      if (gpsHandler && gpsHandler->hasFix()) {
        double distance = gpsHandler->distanceTo(
          track->startFinish.lat,
          track->startFinish.lon
        );

        display->setCursor(0, 36);
        char dist[32];
        if (distance < 1000) {
          snprintf(dist, sizeof(dist), "%.0f m away", distance);
        } else {
          snprintf(dist, sizeof(dist), "%.1f km away", distance / 1000.0);
        }
        drawCentered(String(dist), 36);
      }

      display->setCursor(0, 52);
      drawCentered("Press to start", 52);
    }
  }
}

void DisplayManager::showLapTimingScreen() {
  clear();

  if (!lapTimer) return;

  // Current lap time (large, top)
  display->setTextSize(2);
  String currentTime = LapTimer::formatTime(lapTimer->getCurrentLapTime());
  int16_t x = (SCREEN_WIDTH - currentTime.length() * 12) / 2;
  display->setCursor(x, 0);
  display->println(currentTime);

  display->setTextSize(1);

  // Lap number
  display->setCursor(0, 18);
  display->printf("Lap: %d", lapTimer->getCurrentLapNumber());

  // Last lap
  display->setCursor(0, 28);
  LapData* lastLap = lapTimer->getLastLap();
  if (lastLap) {
    display->printf("Last: %s", LapTimer::formatTime(lastLap->lapTime).c_str());
  } else {
    display->print("Last: --:--:---");
  }

  // Best lap
  display->setCursor(0, 38);
  LapData* bestLap = lapTimer->getBestLap();
  if (bestLap) {
    display->printf("Best: %s", LapTimer::formatTime(bestLap->lapTime).c_str());
  } else {
    display->print("Best: --:--:---");
  }

  // Delta
  display->setCursor(0, 48);
  int32_t delta = lapTimer->getDeltaToBest();
  if (bestLap) {
    display->printf("D: %s", LapTimer::formatDelta(delta).c_str());
  } else {
    display->print("D: ---");
  }

  // Speed
  if (gpsHandler) {
    display->setCursor(0, 56);
    display->printf("SPD: %.1f km/h", gpsHandler->getSpeed());
  }
}

void DisplayManager::showSessionSummaryScreen() {
  clear();
  drawHeader("SESSION END");

  if (lapTimer) {
    int lapCount = lapTimer->getLapCount();

    display->setCursor(0, 18);
    display->printf("Laps: %d", lapCount);

    if (lapCount > 0) {
      LapData* bestLap = lapTimer->getBestLap();
      if (bestLap) {
        display->setCursor(0, 28);
        display->printf("Best: %s", LapTimer::formatTime(bestLap->lapTime).c_str());

        display->setCursor(0, 38);
        display->printf("@ %.1f km/h", bestLap->maxSpeed);
      }

      uint32_t avgTime = lapTimer->getAverageLapTime();
      if (avgTime > 0) {
        display->setCursor(0, 48);
        display->printf("Avg: %s", LapTimer::formatTime(avgTime).c_str());
      }
    }
  }
}

void DisplayManager::clear() {
  if (display) {
    display->clearDisplay();
    display->setTextSize(1);
    display->setCursor(0, 0);
  }
}

void DisplayManager::showMessage(const String& message, uint16_t duration) {
  if (!display) return;

  clear();
  drawCentered(message, SCREEN_HEIGHT / 2 - 4);
  display->display();
  delay(duration);
}

void DisplayManager::drawHeader(const String& title) {
  if (!display) return;

  display->setTextSize(1);
  int16_t x = (SCREEN_WIDTH - title.length() * 6) / 2;
  display->setCursor(x, 0);
  display->println(title);

  // Draw line under header
  display->drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);
}

void DisplayManager::drawCentered(const String& text, int16_t y) {
  if (!display) return;

  int16_t x = (SCREEN_WIDTH - text.length() * 6) / 2;
  display->setCursor(x, y);
  display->println(text);
}

void DisplayManager::drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t progress) {
  if (!display) return;

  progress = min(progress, (uint8_t)100);

  // Draw border
  display->drawRect(x, y, w, h, SSD1306_WHITE);

  // Draw fill
  int16_t fillWidth = ((w - 4) * progress) / 100;
  if (fillWidth > 0) {
    display->fillRect(x + 2, y + 2, fillWidth, h - 4, SSD1306_WHITE);
  }
}
