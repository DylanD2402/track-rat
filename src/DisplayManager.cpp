/**
 * Track Rat - Display Manager Implementation for TFT
 * ST7789 240x320 TFT Display
 */

#include "DisplayManager.h"
#include "Config.h"

DisplayManager::DisplayManager() :
  tft(nullptr),
  currentMode(MODE_BOOT),
  lastUpdate(0),
  updateInterval(DISPLAY_UPDATE_INTERVAL),
  screenWidth(TFT_WIDTH),
  screenHeight(TFT_HEIGHT),
  trackDB(nullptr),
  lapTimer(nullptr),
  gpsHandler(nullptr),
  selectedTrackIndex(0),
  smallFont(2),
  mediumFont(4),
  largeFont(6)
{}

DisplayManager::~DisplayManager() {
  if (tft) {
    delete tft;
  }
}

bool DisplayManager::begin() {
  tft = new TFT_eSPI();

  tft->init();
  tft->setRotation(TFT_ROTATION);
  tft->fillScreen(UI_BACKGROUND);
  tft->setTextColor(UI_TEXT, UI_BACKGROUND);

  // Set backlight
  pinMode(TFT_BL, OUTPUT);
  setBacklight(255);  // Full brightness

  DEBUG_PRINTLN("TFT Display initialized");
  DEBUG_PRINTF("Screen size: %dx%d\n", screenWidth, screenHeight);

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
  if (!tft) return;

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
}

void DisplayManager::showBootScreen() {
  clear();

  // Draw header
  drawHeader("TRACK RAT", UI_HEADER_BG);

  // Logo/Title in center
  tft->setTextColor(UI_HIGHLIGHT);
  tft->setTextDatum(MC_DATUM);
  tft->setTextSize(1);
  tft->drawString("GPS Lap Timer", screenWidth / 2, screenHeight / 2 - 20, 4);

  tft->setTextColor(UI_TEXT);
  tft->drawString("ESP32-S3 Edition", screenWidth / 2, screenHeight / 2 + 20, 2);
  tft->drawString("Initializing...", screenWidth / 2, screenHeight / 2 + 50, 2);

  // Version info at bottom
  tft->setTextDatum(MC_DATUM);
  tft->drawString("v2.0.0", screenWidth / 2, screenHeight - 20, 2);
}

void DisplayManager::showGPSWaitScreen() {
  clear();
  drawHeader("GPS STATUS", UI_HEADER_BG);

  if (gpsHandler) {
    uint8_t sats = gpsHandler->getSatellites();
    bool hasFix = gpsHandler->hasFix();

    // GPS status
    tft->setTextDatum(TL_DATUM);
    tft->setTextColor(UI_TEXT);
    tft->drawString("Satellites:", 20, 50, 4);

    tft->setTextColor(sats >= 4 ? UI_HIGHLIGHT : UI_WARNING);
    tft->drawNumber(sats, 200, 50, 4);

    // Status
    tft->setTextDatum(TL_DATUM);
    tft->setTextColor(UI_TEXT);
    tft->drawString("Status:", 20, 90, 4);

    if (hasFix) {
      tft->setTextColor(UI_HIGHLIGHT);
      tft->drawString("FIX OK", 150, 90, 4);
    } else {
      tft->setTextColor(UI_WARNING);
      tft->drawString("NO FIX", 150, 90, 4);

      // Animated waiting
      uint8_t dots = (millis() / 500) % 4;
      String waiting = "Waiting";
      for (uint8_t i = 0; i < dots; i++) {
        waiting += ".";
      }
      tft->setTextDatum(MC_DATUM);
      tft->drawString(waiting, screenWidth / 2, 140, 2);
    }

    // GPS signal strength indicator
    drawGPSSignal(screenWidth / 2 - 40, 180, sats, hasFix);

    // Progress bar
    uint8_t progress = min(sats * 100 / 8, 100);
    drawProgressBar(20, screenHeight - 60, screenWidth - 40, 30, progress);

    // Progress percentage
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(UI_TEXT);
    char progText[8];
    snprintf(progText, sizeof(progText), "%d%%", progress);
    tft->drawString(progText, screenWidth / 2, screenHeight - 45, 2);
  }
}

void DisplayManager::showTrackSelectionScreen() {
  clear();
  drawHeader("SELECT TRACK", UI_HEADER_BG);

  if (trackDB && trackDB->getTrackCount() > 0) {
    Track* track = trackDB->getTrack(selectedTrackIndex);
    if (track) {
      // Track name (large, centered)
      tft->setTextDatum(MC_DATUM);
      tft->setTextColor(UI_HIGHLIGHT);
      tft->drawString(track->name, screenWidth / 2, 70, 4);

      // Location
      tft->setTextColor(UI_TEXT);
      String location = track->location + ", " + track->country;
      tft->drawString(location, screenWidth / 2, 110, 2);

      // Track info box
      int16_t boxY = 140;
      fillRoundRect(20, boxY, screenWidth - 40, 80, 10, COLOR_DARKGRAY);

      tft->setTextDatum(MC_DATUM);
      tft->setTextColor(UI_TEXT);
      tft->drawString("Track Length", screenWidth / 2, boxY + 20, 2);

      tft->setTextColor(UI_HIGHLIGHT);
      char lengthStr[16];
      snprintf(lengthStr, sizeof(lengthStr), "%.2f km", track->length / 1000.0);
      tft->drawString(lengthStr, screenWidth / 2, boxY + 50, 4);

      // Navigation hint
      tft->setTextDatum(MC_DATUM);
      tft->setTextColor(UI_TEXT);
      char nav[32];
      snprintf(nav, sizeof(nav), "Track %d of %d", selectedTrackIndex + 1, trackDB->getTrackCount());
      tft->drawString(nav, screenWidth / 2, screenHeight - 60, 2);

      // Button hints
      drawButton(20, screenHeight - 40, 80, 30, "< PREV", COLOR_BLUE, COLOR_WHITE);
      drawButton(screenWidth / 2 - 40, screenHeight - 40, 80, 30, "SELECT", COLOR_GREEN, COLOR_WHITE);
      drawButton(screenWidth - 100, screenHeight - 40, 80, 30, "NEXT >", COLOR_BLUE, COLOR_WHITE);
    }
  } else {
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(UI_ERROR);
    tft->drawString("No tracks found!", screenWidth / 2, screenHeight / 2, 4);
  }
}

void DisplayManager::showTrackConfirmScreen() {
  clear();
  drawHeader("CONFIRM TRACK", UI_HEADER_BG);

  if (trackDB) {
    Track* track = trackDB->getTrack(selectedTrackIndex);
    if (track) {
      // Track name
      tft->setTextDatum(MC_DATUM);
      tft->setTextColor(UI_HIGHLIGHT);
      tft->drawString(track->name, screenWidth / 2, 60, 4);

      // Location
      tft->setTextColor(UI_TEXT);
      tft->drawString(track->location, screenWidth / 2, 100, 2);

      // Distance to track (if GPS available)
      if (gpsHandler && gpsHandler->hasFix()) {
        double distance = gpsHandler->distanceTo(
          track->startFinish.lat,
          track->startFinish.lon
        );

        tft->setTextColor(UI_TEXT);
        tft->drawString("Distance to track:", screenWidth / 2, 140, 2);

        char distStr[32];
        if (distance < 1000) {
          snprintf(distStr, sizeof(distStr), "%.0f meters", distance);
        } else {
          snprintf(distStr, sizeof(distStr), "%.1f km", distance / 1000.0);
        }

        uint16_t distColor = distance < 500 ? UI_HIGHLIGHT : UI_WARNING;
        tft->setTextColor(distColor);
        tft->drawString(distStr, screenWidth / 2, 170, 4);
      }

      // Ready indicator
      fillRoundRect(40, screenHeight - 100, screenWidth - 80, 60, 10, COLOR_GREEN);
      tft->setTextColor(COLOR_WHITE);
      tft->drawString("Press SELECT to Start", screenWidth / 2, screenHeight - 70, 2);
    }
  }
}

void DisplayManager::showLapTimingScreen() {
  clear();

  if (!lapTimer) return;

  // Current lap time (huge, top center)
  tft->setTextDatum(MC_DATUM);
  tft->setTextColor(UI_LAP_TIME);
  String currentTime = LapTimer::formatTime(lapTimer->getCurrentLapTime());
  tft->drawString(currentTime, screenWidth / 2, 50, 7);  // Use large 7-segment font

  // Lap number
  tft->setTextDatum(TL_DATUM);
  tft->setTextColor(UI_TEXT);
  tft->drawString("Lap:", 20, 110, 2);
  tft->setTextColor(UI_HIGHLIGHT);
  tft->drawNumber(lapTimer->getCurrentLapNumber(), 80, 110, 4);

  // Last lap
  tft->setTextDatum(TL_DATUM);
  tft->setTextColor(UI_TEXT);
  tft->drawString("Last:", 20, 150, 2);

  LapData* lastLap = lapTimer->getLastLap();
  if (lastLap) {
    tft->setTextColor(UI_TEXT);
    String lastTime = LapTimer::formatTime(lastLap->lapTime);
    tft->drawString(lastTime, 90, 150, 4);
  } else {
    tft->setTextColor(COLOR_GRAY);
    tft->drawString("--:--:---", 90, 150, 4);
  }

  // Best lap
  tft->setTextDatum(TL_DATUM);
  tft->setTextColor(UI_TEXT);
  tft->drawString("Best:", 20, 190, 2);

  LapData* bestLap = lapTimer->getBestLap();
  if (bestLap) {
    tft->setTextColor(UI_HIGHLIGHT);
    String bestTime = LapTimer::formatTime(bestLap->lapTime);
    tft->drawString(bestTime, 90, 190, 4);
  } else {
    tft->setTextColor(COLOR_GRAY);
    tft->drawString("--:--:---", 90, 190, 4);
  }

  // Delta
  tft->setTextDatum(TL_DATUM);
  tft->setTextColor(UI_TEXT);
  tft->drawString("Delta:", 20, 230, 2);

  int32_t delta = lapTimer->getDeltaToBest();
  if (bestLap) {
    String deltaStr = LapTimer::formatDelta(delta);
    uint16_t deltaColor = delta > 0 ? UI_ERROR : UI_HIGHLIGHT;
    tft->setTextColor(deltaColor);
    tft->drawString(deltaStr, 110, 230, 4);
  } else {
    tft->setTextColor(COLOR_GRAY);
    tft->drawString("---", 110, 230, 4);
  }

  // Speed
  if (gpsHandler) {
    tft->setTextDatum(TL_DATUM);
    tft->setTextColor(UI_TEXT);
    tft->drawString("Speed:", 20, 270, 2);

    tft->setTextColor(UI_HIGHLIGHT);
    char speedStr[16];
    snprintf(speedStr, sizeof(speedStr), "%.1f km/h", gpsHandler->getSpeed());
    tft->drawString(speedStr, 110, 270, 4);
  }
}

void DisplayManager::showSessionSummaryScreen() {
  clear();
  drawHeader("SESSION SUMMARY", UI_HEADER_BG);

  if (lapTimer) {
    int lapCount = lapTimer->getLapCount();

    // Total laps
    tft->setTextDatum(TL_DATUM);
    tft->setTextColor(UI_TEXT);
    tft->drawString("Total Laps:", 20, 60, 2);
    tft->setTextColor(UI_HIGHLIGHT);
    tft->drawNumber(lapCount, 180, 60, 4);

    if (lapCount > 0) {
      // Best lap
      LapData* bestLap = lapTimer->getBestLap();
      if (bestLap) {
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(UI_TEXT);
        tft->drawString("Best Lap:", 20, 110, 2);

        tft->setTextColor(UI_HIGHLIGHT);
        String bestTime = LapTimer::formatTime(bestLap->lapTime);
        tft->drawString(bestTime, 20, 140, 4);

        // Best lap speed
        char speedStr[32];
        snprintf(speedStr, sizeof(speedStr), "@ %.1f km/h", bestLap->maxSpeed);
        tft->setTextColor(UI_TEXT);
        tft->drawString(speedStr, 20, 180, 2);
      }

      // Average lap
      uint32_t avgTime = lapTimer->getAverageLapTime();
      if (avgTime > 0) {
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(UI_TEXT);
        tft->drawString("Average Lap:", 20, 220, 2);

        String avgTimeStr = LapTimer::formatTime(avgTime);
        tft->drawString(avgTimeStr, 20, 250, 4);
      }
    }

    // Footer message
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(UI_TEXT);
    tft->drawString("Session saved to SD card", screenWidth / 2, screenHeight - 30, 2);
  }
}

void DisplayManager::clear() {
  if (tft) {
    tft->fillScreen(UI_BACKGROUND);
  }
}

void DisplayManager::showMessage(const String& message, uint16_t duration, uint16_t color) {
  if (!tft) return;

  clear();
  tft->setTextDatum(MC_DATUM);
  tft->setTextColor(color);
  tft->drawString(message, screenWidth / 2, screenHeight / 2, 4);
  delay(duration);
}

void DisplayManager::setBacklight(uint8_t brightness) {
  analogWrite(TFT_BL, brightness);
}

void DisplayManager::drawHeader(const String& title, uint16_t bgColor) {
  if (!tft) return;

  // Draw header background
  tft->fillRect(0, 0, screenWidth, 40, bgColor);

  // Draw title
  tft->setTextDatum(MC_DATUM);
  tft->setTextColor(UI_HEADER_TEXT);
  tft->drawString(title, screenWidth / 2, 20, 4);

  // Draw separator line
  tft->drawLine(0, 40, screenWidth, 40, UI_TEXT);
}

void DisplayManager::drawCentered(const String& text, int16_t y, uint16_t color, uint8_t font) {
  if (!tft) return;

  tft->setTextDatum(MC_DATUM);
  tft->setTextColor(color);
  tft->drawString(text, screenWidth / 2, y, font);
}

void DisplayManager::drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t progress,
                                     uint16_t fillColor, uint16_t bgColor) {
  if (!tft) return;

  progress = min(progress, (uint8_t)100);

  // Draw background
  tft->fillRect(x, y, w, h, bgColor);

  // Draw fill
  int16_t fillWidth = (w * progress) / 100;
  if (fillWidth > 0) {
    tft->fillRect(x, y, fillWidth, h, fillColor);
  }

  // Draw border
  tft->drawRect(x, y, w, h, UI_TEXT);
}

void DisplayManager::drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const String& label,
                                uint16_t bgColor, uint16_t textColor) {
  if (!tft) return;

  fillRoundRect(x, y, w, h, 5, bgColor);
  tft->setTextDatum(MC_DATUM);
  tft->setTextColor(textColor);
  tft->drawString(label, x + w / 2, y + h / 2, 2);
}

void DisplayManager::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
  if (!tft) return;

  tft->fillRoundRect(x, y, w, h, r, color);
}

void DisplayManager::drawGPSSignal(int16_t x, int16_t y, uint8_t satellites, bool hasFix) {
  if (!tft) return;

  // Draw 5 signal bars
  uint8_t barWidth = 12;
  uint8_t barSpacing = 4;
  uint8_t maxHeight = 40;

  for (uint8_t i = 0; i < 5; i++) {
    uint8_t barHeight = maxHeight * (i + 1) / 5;
    uint16_t color;

    if (i < satellites / 2) {
      color = hasFix ? UI_HIGHLIGHT : UI_WARNING;
    } else {
      color = COLOR_DARKGRAY;
    }

    tft->fillRect(x + i * (barWidth + barSpacing), y + (maxHeight - barHeight),
                  barWidth, barHeight, color);
  }
}
