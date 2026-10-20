/**
 * Track Rat - Display
 *
 * ILI9341V 320x240 (landscape) via TFT_eSPI. Every screen is drawn into a
 * full-screen sprite in PSRAM and pushed in one go, so there is no flicker.
 * Screens are stateless: main.cpp passes in what to show.
 *
 * Text is Inter, anti-aliased (TFT_eSPI smooth fonts made by
 * tools/make_fonts.py). Colours and layout are in UiTheme.h.
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "FreeRoam.h"
#include "GPSHandler.h"
#include "LapTimer.h"
#include "PerfRun.h"
#include "SettingsData.h"
#include "SettingsFlow.h"
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
  String notice;  // e.g. "Layout: Short" after switching layout
};

class DisplayManager {
public:
  // Text styles, in the order of the fonts made by tools/make_fonts.py.
  // NumL and NumXL have digits and ":.+-%" only; Hero has capitals and digits only.
  enum class Font : uint8_t { Caption, Body, Button, Title, Hero, NumL, NumXL };
  static const uint8_t* fontData(Font f);  // for TFT_eSPI loadFont() on another sprite (the splash)

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
  // layoutNumber is 0-based; layoutCount > 1 offers "tap the top bar for the next layout".
  // fromList: reached from the track list, so the left button is "Back" (otherwise "Menu").
  // autoStart: waiting for the car to drive off, which starts the session by itself.
  void drawTrackConfirm(const Track& track, double distanceM, bool gpsFix, bool touchOk, int layoutNumber = 0,
                        int layoutCount = 1, bool fromList = false, bool autoStart = false);
  void drawRunning(const Track& track, const LapTimer& timer, const GPSHandler& gps,
                   const RunningOverlay& overlay, uint32_t holdMs);
  void drawRunningMap(const Track& track, const LapTimer& timer, const GPSHandler& gps,
                      const RunningOverlay& overlay, uint32_t holdMs);
  void drawSummary(const Track& track, const LapTimer& timer, const String& savedPath);
  // Mode menu: highlight is for BOOT-button navigation (0..5: the three tiles, then Performance, Settings, Wi-Fi)
  void drawMenu(int highlight, const GPSHandler& gps, bool noTrackNearby, bool touchOk);
  void drawFreeRoam(const FreeRoam& fr, const GPSHandler& gps, uint32_t holdMs);
  void drawFreeRoamSummary(const FreeRoam& fr, uint32_t durationMs, const String& savedPath);
  void drawSetLine(const Track& track, const GPSHandler& gps, uint32_t holdMs);
  // Performance mode (DisplayPerf.cpp). Card: a run by index, or the session's best when index < 0.
  void drawPerfLive(const perf::PerfRun& pr, const GPSHandler& gps, perf::SpeedUnit units, const char* leftButton,
                    uint32_t holdMs, bool touchOk);
  void drawPerfCard(const perf::PerfRun& pr, int index, perf::SpeedUnit units, bool touchOk);

  // Battery, sleep and firmware update screens (DisplayPower.cpp)
  void setBatteryPercent(int percent) { batteryPct = percent; }  // shown in the header; -1 = no battery fitted
  void setBacklight(bool on);
  void powerDown();  // blank the panel and put it to sleep (before deep sleep)
  void drawMessage(const char* title, const char* line1, const char* line2, uint16_t color);
  void drawFirmwarePrompt(const char* modified, uint32_t sizeBytes, bool touchOk);
  void drawFirmwareProgress(int percent);
  // Wi-Fi screen: network name, address and upload code. progress: firmware install, -1 = none
  void drawWifi(bool wifiOn, const String& network, const String& address, int phones, int uploadCode,
                int progress, bool restarting, bool touchOk);

  // Settings (DisplaySettings.cpp). The screen turns round at once when the Screen setting changes.
  void setRotation(uint8_t r);
  // The scrolling list, or the page of one setting, as ui says. localMs: local time of day, or -1.
  void drawSettings(const SettingsData& s, const SettingsScreenState& ui, bool touchOk, int64_t localMs);
  // First boot: step 0 asks which way up (highlight 0 = the left arrow, 1 = the right), step 1 km/h or mph.
  void drawSetup(int step, int highlight, bool touchOk);

  // WI-FI buttons, in the right third: on the menu from MENU_WIFI_Y to
  // MENU_WIFI_Y + MENU_ROW_H, on the summaries from WIFI_BUTTON_Y to the bottom
  static constexpr int MENU_WIFI_Y = 154;
  static constexpr int MENU_ROW_H = 34;
  static constexpr int WIFI_BUTTON_Y = 196;

private:
  void wifiButton(int y, int h, bool highlight, bool holdHint = false);
  TFT_eSPI tft;
  TFT_eSprite spr = TFT_eSprite(&tft);
  bool useSprite = false;
  TFT_eSPI* gfx = &tft;  // sprite if available, else the panel directly
  uint16_t* shown = nullptr;  // copy of the panel contents (PSRAM)
  bool shownValid = false;
  int batteryPct = -1;
  uint8_t rotation = DISPLAY_ROTATION;
  void flipBand(int y, int h);  // turns rows y..y+h-1 of the frame upside down (for the setup screen)

  int loadedFont = -1;
  void useFont(Font f);
  // Draws s with its `datum` point (TFT_eSPI TL_DATUM, MC_DATUM...) at x, y. Returns its width.
  int text(const String& s, int x, int y, Font f, uint16_t color, uint8_t datum = TL_DATUM);
  int textWidth(const String& s, Font f);
  String fit(const String& s, Font f, int maxW);  // shortened with "…" to fit maxW
  void label(const char* s, int x, int y, uint8_t datum = TL_DATUM);  // small grey caption

  void beginFrame();
  void endFrame();
  void header(const String& title, const GPSHandler* gps);
  void gpsBadge(const GPSHandler& gps);
  void diffBox(int32_t delta, bool lapRunning, const RunningOverlay& ov);
  struct MapPath {
    const uint8_t* base;  // points start with float x, y (metres)
    size_t n;
    size_t stride;
  };
  bool drawMapArea(int x, int y, int w, int h, const std::vector<std::vector<geo::XYf>>& lines, MapPath path,
                   bool showCar, geo::XY car, bool carFix, bool showStartFinish);
  void runningOverlays(const GPSHandler& gps, uint32_t holdMs, const RunningOverlay* ov = nullptr);
  String runningTitle(const Track& track, const LapTimer& timer);
  void footerButtons(const char* left, const char* center, const char* right);
  // Settings (DisplaySettings.cpp)
  void settingsList(const SettingsData& s, const SettingsScreenState& ui, bool touchOk);
  void settingsChoicePage(const SettingsData& s, const SettingsScreenState& ui, bool touchOk);
  void settingsLevelPage(const SettingsData& s, const SettingsScreenState& ui, bool touchOk, int64_t localMs);
  void settingsHint(const char* line);
  void wrappedCaption(const char* note, int x, int y, int maxW, uint16_t color, bool center);
  void button(int x, int y, int w, int h, const char* label, bool primary, bool highlight = false);
  void holdBar(uint32_t holdMs, const char* what);  // progress while the screen is held
  // Centred on the screen; a Title too wide for the screen drops to Body, then is shortened
  void centered(const String& s, int y, Font f, uint16_t color);
  // Performance cards (DisplayPerf.cpp)
  void perfBigValue(float value, int decimals, const char* unit, int y, uint16_t color);
  void perfRow(int y, const char* label, const String& value, const String& extra, uint16_t valueColor);
  String distanceText(double m);
};

#endif  // DISPLAY_MANAGER_H
