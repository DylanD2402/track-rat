/**
 * Track Rat - Display Manager
 * Handles OLED display and GUI for track selection and lap timing
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "TrackDatabase.h"
#include "LapTimer.h"
#include "GPSHandler.h"

enum DisplayMode {
  MODE_BOOT,
  MODE_GPS_WAITING,
  MODE_TRACK_SELECTION,
  MODE_TRACK_CONFIRM,
  MODE_LAP_TIMING,
  MODE_SESSION_SUMMARY
};

class DisplayManager {
private:
  Adafruit_SSD1306* display;
  DisplayMode currentMode;
  unsigned long lastUpdate;
  uint16_t updateInterval;

  // References to other components
  TrackDatabase* trackDB;
  LapTimer* lapTimer;
  GPSHandler* gpsHandler;

  // Track selection
  int selectedTrackIndex;

  // Helper methods
  void drawHeader(const String& title);
  void drawCentered(const String& text, int16_t y);
  void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t progress);

public:
  DisplayManager();
  ~DisplayManager();

  bool begin(uint8_t sda, uint8_t scl, uint8_t address = 0x3C);
  void setComponents(TrackDatabase* db, LapTimer* timer, GPSHandler* gps);

  // Mode management
  void setMode(DisplayMode mode);
  DisplayMode getMode() const;

  // Track selection
  void nextTrack();
  void previousTrack();
  int getSelectedTrackIndex() const;

  // Update display
  void update();

  // Individual screens
  void showBootScreen();
  void showGPSWaitScreen();
  void showTrackSelectionScreen();
  void showTrackConfirmScreen();
  void showLapTimingScreen();
  void showSessionSummaryScreen();

  // Utility
  void clear();
  void showMessage(const String& message, uint16_t duration = 2000);
};

#endif // DISPLAY_MANAGER_H
