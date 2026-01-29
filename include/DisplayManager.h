/**
 * Track Rat - Display Manager for TFT Display
 * Handles ST7789 TFT display (240x320) and GUI for track selection and lap timing
 * Hardware: ESP32-S3 with built-in TFT display
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <TFT_eSPI.h>
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
  TFT_eSPI* tft;
  DisplayMode currentMode;
  unsigned long lastUpdate;
  uint16_t updateInterval;

  // Screen dimensions
  int16_t screenWidth;
  int16_t screenHeight;

  // References to other components
  TrackDatabase* trackDB;
  LapTimer* lapTimer;
  GPSHandler* gpsHandler;

  // Track selection
  int selectedTrackIndex;

  // Font sizes
  uint8_t smallFont;
  uint8_t mediumFont;
  uint8_t largeFont;

  // Helper methods
  void drawHeader(const String& title, uint16_t bgColor = 0x001F);
  void drawCentered(const String& text, int16_t y, uint16_t color = 0xFFFF, uint8_t font = 2);
  void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t progress,
                       uint16_t fillColor = 0x07E0, uint16_t bgColor = 0x4208);
  void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const String& label,
                  uint16_t bgColor, uint16_t textColor);
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);

  // GPS signal indicator
  void drawGPSSignal(int16_t x, int16_t y, uint8_t satellites, bool hasFix);

public:
  DisplayManager();
  ~DisplayManager();

  bool begin();
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
  void showMessage(const String& message, uint16_t duration = 2000, uint16_t color = 0xFFFF);
  void setBacklight(uint8_t brightness);  // 0-255
};

#endif // DISPLAY_MANAGER_H
