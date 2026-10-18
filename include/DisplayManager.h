/**
 * Track Rat - Display
 *
 * ILI9341V 320x240 (landscape) via TFT_eSPI. Every screen is drawn into a
 * full-screen sprite in PSRAM and pushed in one go, so there is no flicker.
 * Screens are stateless: main.cpp passes in what to show.
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "GPSHandler.h"
#include "LapTimer.h"
#include "TrackDatabase.h"

struct RunningOverlay {
  // Shown for a few seconds after a lap or split
  bool showLap = false;
  uint16_t lapNumber = 0;
  uint32_t lapMs = 0;
  bool lapIsBest = false;
  int32_t deltaMs = NO_DELTA;  // lap delta vs best, or split delta
  int splitIndex = -1;         // >= 0: delta box shows a split
  uint32_t splitMs = 0;
};

class DisplayManager {
public:
  bool begin();

  void drawBoot(const char* status);

  // Start-up logo animation (Splash.cpp). Runs on the other core; drawing
  // anything else before finishSplash() returns would clash with it.
  void startSplash();
  void finishSplash();
  bool splashRunning() const;
  // statusOnly: the GPS Status screen opened from the track list (no auto-advance)
  void drawGpsWait(const GPSHandler& gps, int trackCount, const char* trackSource, bool touchOk, bool statusOnly);
  void drawTrackSelect(const Track& track, int index, int count, double distanceM, const GPSHandler& gps,
                       bool touchOk);
  void drawTrackConfirm(const Track& track, double distanceM, bool gpsFix, bool touchOk);
  void drawRunning(const Track& track, const LapTimer& timer, const GPSHandler& gps,
                   const RunningOverlay& overlay, uint32_t holdMs);
  void drawRunningMap(const Track& track, const LapTimer& timer, const GPSHandler& gps,
                      const RunningOverlay& overlay, uint32_t holdMs);
  void drawSummary(const Track& track, const LapTimer& timer, const String& savedPath);

private:
  TFT_eSPI tft;
  TFT_eSprite spr = TFT_eSprite(&tft);
  bool useSprite = false;
  TFT_eSPI* gfx = &tft;  // sprite if available, else the panel directly
  uint16_t* shown = nullptr;  // copy of the panel contents (PSRAM)
  bool shownValid = false;

  void beginFrame();
  void endFrame();
  void header(const String& title, const GPSHandler* gps);
  void gpsBadge(const GPSHandler& gps);
  void diffBox(int32_t delta, bool lapRunning, const RunningOverlay& ov);
  void runningOverlays(const GPSHandler& gps, uint32_t holdMs);
  String runningTitle(const Track& track, const LapTimer& timer);
  void footerButtons(const char* left, const char* center, const char* right);
  void centered(const String& s, int y, int font, uint16_t color);
  String distanceText(double m);
};

#endif  // DISPLAY_MANAGER_H
