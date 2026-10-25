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
#include "NextUp.h"
#include "PerfRun.h"
#include "SettingsData.h"
#include "SettingsFlow.h"
#include "DeviceScreen.h"
#include "ObdBle.h"
#include "CameraBle.h"
#include "TrackDatabase.h"
#include "Debrief.h"
#include "Courses.h"

struct RunningOverlay {
  // Shown for a few seconds after a lap or split
  bool showLap = false;
  uint16_t lapNumber = 0;
  uint32_t lapMs = 0;
  bool lapIsBest = false;
  bool lapIsPb = false;        // a new personal best: purple
  bool lapIsDayBest = false;   // the best of the day: green (blue)
  int32_t lapDeltaMs = NO_DELTA;  // the lap's gap to the reference it was timed against
  int32_t deltaMs = NO_DELTA;  // split delta
  int splitIndex = -1;         // >= 0: delta box shows a split
  uint32_t splitMs = 0;
  bool isRun = false;  // a point-to-point run (the flash says RUN, not LAP); lapNumber is the day's run number
  String notice;  // e.g. "Layout: Short" after switching layout
  // Kept while timing (TimingScreen.h)
  int32_t trendMs = INT32_MIN;          // the delta's change over the last few seconds (timing::NO_TREND: none)
  std::vector<int32_t> sectorBestEver;  // per corner sector, -1: none (the sector strip)
  std::vector<int32_t> sectorBestDay;
};

// What the pit debrief shows (worked out by main.cpp when the car parks; see Debrief.h).
struct DebriefView {
  debrief::Result r;
  std::string hints[3];               // for r.top, "" if none
  uint32_t allTimeTheoreticalMs = 0;  // 0: not known
  int session = 0;
  int lastSession = 0;                // the session before at this track (0: none)
  uint32_t lastBestMs = 0;
  bool lastToday = false;
};

// What the Autocross hub shows (DisplayAutocross.cpp). main.cpp chooses the course and works out the
// runs (Courses.h, CourseRuns.h).
struct AutocrossView {
  bool haveCourse = false;
  String name;            // "Oct 25 course"
  String dateText;        // "Oct 20": shown when the course isn't today's
  bool today = false;     // made today: runs go on from the list
  int nextRun = 0;        // "Run N next" (today's course)
  bool loading = false;   // the runs are still being read from the card
  std::vector<courses::Run> runs;
  int best = -1, last = -1;  // indexes into runs
  bool gpsFix = false;
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
  // statusOnly: the GPS Status screen opened from the menu's top bar (no auto-advance)
  void drawGpsWait(const GPSHandler& gps, int trackCount, const char* trackSource, bool touchOk, bool statusOnly);
  // The track list and Ready screens show this track's PB: set it first (0 = none).
  void setPersonalBest(uint32_t lapMs, uint32_t date) {
    pbMs = lapMs;
    pbDate = date;
  }
  // The same two screens show "driver · car" under the PB once there is a choice of either (Profiles.h):
  // set it first ("" = none).
  void setDriverCar(const char* label) { snprintf(driverCar, sizeof(driverCar), "%s", label); }
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
  // The slow screen (SituationScreen.h): big speed, last lap and the session clock.
  // inPitLane: the track's map says the car is in the pit lane.
  void drawSlow(const Track& track, const LapTimer& timer, const GPSHandler& gps, bool inPitLane, uint32_t holdMs);
  // The session clock in the timing screens' header ("12:34", "7:26 left"); "" for none.
  // ending: under a minute left, drawn yellow.
  void setSessionClock(const String& text, bool ending) {
    sessionClock = text;
    sessionClockEnding = ending;
  }
  // The lap summary, with each lap's tags. inPits: shown while parked in
  // the session (driving off carries on), so no buttons. session: "Session
  // N" of the day, 0 if not known.
  void drawSummary(const Track& track, const LapTimer& timer, const String& savedPath, int session, bool inPits);
  // The SYNC flash (6.4): white, SYNC and the GPS time ("14:03:12.3"), for a camera to film
  void drawSync(const char* gpsTime);
  // The overheat warning (6.5): COOLANT (or OIL) TOO HOT and the temperature, for TEMP_ALERT_MS,
  // on red or yellow (red: TempAlert's redPhase) so it flashes
  void drawTempAlert(bool oil, float c, bool red);
  // The pit debrief (DisplayDebrief.cpp). Page 0: best lap, theoretical best and the three
  // sectors that cost most, on the map. Page 1: every lap with its tags, every sector, and
  // this session against the last.
  void drawDebrief(const Track& track, const LapTimer& timer, const DebriefView& d, int page);
  // The home screen (DisplayHome.cpp; places in HomeLayout.h): the Next up card, four tiles, Wi-Fi and Settings.
  // highlight is for BOOT-button navigation (home::Item, -1 none).
  void drawHome(const nextup::Choice& next, int highlight, const GPSHandler& gps, bool touchOk);
  // The Track day list's two entries that are not a track: "+ New circuit" (back false) and, for a unit
  // with no touch screen, "Back" (back true). index of count is the place in the list.
  void drawTrackDayExtra(bool back, int index, int count, const GPSHandler& gps, bool touchOk);
  void drawFreeRoam(const FreeRoam& fr, const GPSHandler& gps, uint32_t holdMs);
  void drawFreeRoamSummary(const FreeRoam& fr, uint32_t durationMs, const String& savedPath);
  void drawSetLine(const Track& track, const GPSHandler& gps, uint32_t holdMs);
  // The Autocross hub. highlight (BOOT button): 0 Start runs, 1 New course, 2 Adjust start, 3 Adjust finish,
  // 4 Back; -1 none. Adjust start and Adjust finish are shown but not wired yet ("coming soon").
  void drawAutocross(const AutocrossView& v, const GPSHandler& gps, int highlight, bool touchOk);
  // New course: park at the start and press Set start here. ready: has a fix and is stopped.
  // highlight: 0 Back, 1 Set start here.
  void drawNewCourse(const GPSHandler& gps, bool ready, int highlight, bool touchOk);
  // The timing screens count runs, not laps, on a point-to-point track: the runs of the day before this
  // session (CourseRuns.h; 0 for a hill climb or sprint). "Run 4 next" shows while waiting for a run.
  void setRunBase(int before) { runBase = before; }
  // The finish was just set: Undo for this long (0: not showing). Drawn over the timing screens.
  void setFinishUndo(uint32_t msLeft, bool touchOk) {
    undoMsLeft = msLeft;
    undoTouch = touchOk;
  }
  // Performance mode (DisplayPerf.cpp). Card: a run by index, or the session's best when index < 0.
  void drawPerfLive(const perf::PerfRun& pr, const GPSHandler& gps, perf::SpeedUnit units, const char* leftButton,
                    uint32_t holdMs, bool touchOk);
  void drawPerfCard(const perf::PerfRun& pr, int index, perf::SpeedUnit units, bool touchOk);

  // Battery, sleep and firmware update screens (DisplayPower.cpp)
  void setBatteryPercent(int percent) { batteryPct = percent; }  // shown in the header; -1 = no battery fitted
  // The camera's CAM/REC tag in the top bar: whether a tap at x hit it (drawn in the last frame)
  bool cameraTagHit(int x) const { return camTagL >= 0 && x >= camTagL - 8 && x <= camTagR + 8; }
  // It was tapped: "REC..." or "STOP..." until the camera's status follows
  void cameraAsked(bool wasRecording);
  void setBacklight(bool on);
  void powerDown();  // blank the panel and put it to sleep (before deep sleep)
  void drawMessage(const char* title, const char* line1, const char* line2, uint16_t color);
  // BOOT held to switch off: a bar fills from POWER_OFF_BAR_MS (when this first shows) to POWER_OFF_HOLD_MS
  void drawPowerHold(uint32_t heldMs);
  void drawFirmwarePrompt(const char* modified, uint32_t sizeBytes, bool touchOk);
  void drawFirmwareProgress(int percent);
  // Wi-Fi screen: network name, address and upload code. progress: firmware install, -1 = none
  void drawWifi(bool wifiOn, const String& network, const String& address, int phones, int uploadCode,
                int progress, bool restarting, bool touchOk);

  // Settings (DisplaySettings.cpp). The screen turns round at once when the Screen setting changes.
  void setRotation(uint8_t r);
  // The scrolling list, or the page of one setting, as ui says. localMs: local time of day, or -1.
  void drawSettings(const SettingsData& s, const SettingsScreenState& ui, bool touchOk, int64_t localMs);
  // Settings > Engine data: the adapter's status, or the Find adapter list (found: n of them)
  void drawEngineData(const DeviceScreenState& ui, const ObdBle::Status& st, const ObdBle::Found* found, int n, bool scanning, bool touchOk);
  // Settings > Camera: the DJI camera's status and pairing code, or the Pair list
  void drawCamera(const DeviceScreenState& ui, const CameraBle::Status& st, bool withSessions, const CameraBle::Found* found, int n,
                  bool scanning, bool touchOk);
  // First boot: step 0 asks which way up (highlight 0 = the left arrow, 1 = the right), step 1 km/h or mph.
  void drawSetup(int step, int highlight, bool touchOk);

  // WI-FI buttons, in the right third, on the summaries from WIFI_BUTTON_Y to the bottom
  static constexpr int WIFI_BUTTON_Y = 196;

private:
  uint32_t pbMs = 0, pbDate = 0;  // setPersonalBest
  char driverCar[48] = "";        // setDriverCar
  int runBase = 0;                 // setRunBase
  uint32_t undoMsLeft = 0;         // setFinishUndo
  bool undoTouch = true;
  String sessionClock;             // setSessionClock
  bool sessionClockEnding = false;
  String pbText() const;          // "PB 1:42.345  ·  19 Oct 2026"
  // x and w: where, if not the right third
  void wifiButton(int y, int h, bool highlight, bool holdHint = false, int x = -1, int w = -1);
  TFT_eSPI tft;
  TFT_eSprite spr = TFT_eSprite(&tft);
  bool useSprite = false;
  TFT_eSPI* gfx = &tft;  // sprite if available, else the panel directly
  uint16_t* shown = nullptr;  // copy of the panel contents (PSRAM)
  bool shownValid = false;
  int batteryPct = -1;
  int camTagL = -1, camTagR = -1;  // the camera's tag in the top bar (-1: not drawn)
  uint32_t camAskedAt = 0;         // millis() of a tap on it (0: none)
  bool camAskedWasRec = false;     // recording when tapped
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
  void header(const String& title, const GPSHandler* gps, uint16_t titleColor = 0xFFFF);
  void gpsBadge(const GPSHandler& gps);
  struct MapPath {
    const uint8_t* base;  // points start with float x, y (metres)
    size_t n;
    size_t stride;
  };
  // A stretch of the path drawn in colour, with a numbered dot (the debrief's sectors).
  struct MapMark {
    size_t from, to;  // path indices
    uint16_t color;
    int number;
  };
  // ghost: where the reference lap was at this moment (a hollow ring), if not null
  bool drawMapArea(int x, int y, int w, int h, const std::vector<std::vector<geo::XYf>>& lines, MapPath path,
                   bool showCar, geo::XY car, bool carFix, bool showStartFinish,
                   const std::vector<MapMark>* marks = nullptr, const geo::XY* ghost = nullptr);
  void runningOverlays(const GPSHandler& gps, uint32_t holdMs, const RunningOverlay* ov = nullptr);
  String runningTitle(const Track& track, const LapTimer& timer);
  void footerButtons(const char* left, const char* center, const char* right);
  // Settings (DisplaySettings.cpp)
  void settingsList(const SettingsData& s, const SettingsScreenState& ui, bool touchOk);
  void settingsChoicePage(const SettingsData& s, const SettingsScreenState& ui, bool touchOk);
  void settingsPickPage(const SettingsScreenState& ui, bool touchOk);
  void settingsLevelPage(const SettingsData& s, const SettingsScreenState& ui, bool touchOk, int64_t localMs);
  void settingsCoolantPage(const SettingsData& s, const SettingsScreenState& ui, bool touchOk);
  void settingsPowerOffPage(bool touchOk);
  void titlePill(const char* title, uint16_t color);  // drawMessage's coloured title, near the top
  // The engine's temperatures (6.5): a colour for each band, and the session's hottest
  // ("Coolant max 104°   ·   oil 128°") right-aligned at `right`. Returns its width, 0 if none.
  static uint16_t tempColor(float c, bool oil);
  int tempMaxLine(const LapTimer& timer, int right, int y);
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
