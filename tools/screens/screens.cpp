// Renders every screen of the display code on a PC, with made-up but
// realistic data: a car driven round a real track (from data/tracks.json)
// through the real LapTimer, and runs through the real PerfRun and FreeRoam.
// Built and run by tools/render_screens.py, which also writes track_data.h.

#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include "Arduino.h"
#include "TFT_eSPI.h"

// The screens read the GPS through getters; set what they read directly.
#define private public
#include "DisplayManager.h"
#include "SessionLog.h"
#undef private
#include "Power.h"
#include "RaceBoxBle.h"
#include "EngineData.h"
#include "ObdBle.h"
#include "CameraBle.h"
#include "HomeLayout.h"
#include "NextUp.h"
#include "Settings.h"
#include "Storage.h"
#include "Sectors.h"
#include "AdjustLine.h"
#include "TimingScreen.h"
#include "UiTheme.h"
#include "track_data.h"  // TRACK_NAME, TRACK_LOCATION, TRACK_LENGTH_M, SF_LAT, SF_LON, TRACK_PTS (lat, lon pairs)

uint32_t mockMillis = 0;
static bool btConnected = false;

// --- stand-ins for the parts of the firmware the screens call ---------------
bool GPSHandler::hasFix() const { return everFixed; }
bool GPSHandler::receivingData() const { return simulating || lastSentenceMillis != 0; }
bool DisplayManager::splashRunning() const { return false; }
#include "BrandDraw.h"
#include "logo_image.h"
void DisplayManager::drawBrandLogo(int x, int y) { drawLogoPixels(gfx, LOGO_PIXELS, LOGO_W, LOGO_H, x - LOGO_W / 2, y); }
const char* Storage::sessionFSName() { return "SD"; }
bool RaceBoxBle::connected() { return btConnected; }
static ObdBle::Status obdStatus;
static obd::Values obdValues;
bool ObdBle::connected() { return obdStatus.connected; }
bool ObdBle::readingEngine() { return obdStatus.connected && elm::Client::engineAnswering(obdStatus.phase); }
ObdBle::Status ObdBle::status() { return obdStatus; }
static CameraBle::Status camStatus;
bool CameraBle::connected() { return camStatus.connected; }
CameraBle::Status CameraBle::status() { return camStatus; }
static bool coolantKnown = false, oilKnown = false;  // the engine's temperatures in the top bar (6.5)
bool EngineData::coolant(uint32_t, float& c) { return coolantKnown && (c = obdValues.coolantC, true); }
bool EngineData::oil(uint32_t, float& c) { return oilKnown && (c = obdValues.oilC, true); }
bool EngineData::carValue(int, uint32_t, float&) { return false; }  // (the Engine data screen lists the car's own values)
void Power::backlight(bool) {}
static SettingsData mockSettings = [] {
  SettingsData s = settings::defaults();
  s.setupDone = true;
  return s;
}();
SettingsData Settings::get() { return mockSettings; }

// As in SessionLog.cpp
String formatLapTime(uint32_t ms, int decimals) {
  uint32_t minutes = ms / 60000, seconds = (ms / 1000) % 60, frac = ms % 1000;
  char buf[20];
  if (decimals <= 0) snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
  else if (decimals == 1) snprintf(buf, sizeof(buf), "%lu:%02lu.%01lu", (unsigned long)minutes, (unsigned long)seconds, (unsigned long)(frac / 100));
  else snprintf(buf, sizeof(buf), "%lu:%02lu.%03lu", (unsigned long)minutes, (unsigned long)seconds, (unsigned long)frac);
  return String(buf);
}
String formatDelta(int32_t ms) {
  if (ms == NO_DELTA) return String("--");
  char buf[16];
  uint32_t a = ms < 0 ? -ms : ms;
  snprintf(buf, sizeof(buf), "%c%lu.%03lu", ms < 0 ? '-' : '+', (unsigned long)(a / 1000), (unsigned long)(a % 1000));
  return String(buf);
}

// --- output -----------------------------------------------------------------
static std::string outDir;

static void save(DisplayManager& d, const char* name) {
  std::string path = outDir + "/" + name + ".ppm";
  FILE* f = fopen(path.c_str(), "wb");
  fprintf(f, "P6\n320 240\n255\n");
  const uint16_t* p = d.tft.pixels();
  for (int i = 0; i < 320 * 240; i++) {
    uint16_t c = p[i];
    uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

// --- a GPS with a fix --------------------------------------------------------
static void setGps(GPSHandler& g, bool fix, double lat, double lon, float kmh, uint32_t gpsMs) {
  g.everFixed = fix;
  g.lastSentenceMillis = 1;
  g.sats = fix ? 14 : 3;
  g.hdopVal = fix ? 1.1f : 9.9f;
  g.rateHz = fix ? 20.0f : 0.0f;
  g.baudRate = 115200;
  g.rxPin = 44;
  g.source.active = true;
  g.last = {};
  g.last.lat = lat;
  g.last.lon = lon;
  g.last.speedKmh = kmh;
  g.last.gpsTimeMs = gpsMs;
  g.last.rxMillis = fix ? mockMillis : 0;
  g.last.hAccM = fix ? 1.4f : -1;
  g.last.speedAccMs = 0.15f;
  g.last.fixType = 3;
}

// --- the track ----------------------------------------------------------------
struct Lap1 {
  std::vector<double> lat, lon, dist;  // the loop, starting at the start/finish
  double length = 0;
  // Starts at the point of the outline closest to the start/finish, which
  // can be part way along a long straight between two of its points.
  void build() {
    size_t n = sizeof(TRACK_PTS) / sizeof(TRACK_PTS[0]) / 2, seg = 0;
    double bestD = 1e18, bestU = 0;
    for (size_t i = 0; i + 1 < n; i++) {
      geo::XY a = geo::toLocal(TRACK_PTS[2 * i], TRACK_PTS[2 * i + 1], SF_LAT, SF_LON);
      geo::XY b = geo::toLocal(TRACK_PTS[2 * i + 2], TRACK_PTS[2 * i + 3], SF_LAT, SF_LON);
      double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
      double u = len2 > 0 ? std::min(1.0, std::max(0.0, -(a.x * dx + a.y * dy) / len2)) : 0;
      double d = hypot(a.x + u * dx, a.y + u * dy);
      if (d < bestD) bestD = d, seg = i, bestU = u;
    }
    auto add = [&](double la, double lo) {
      if (!lat.empty()) length += geo::distanceM(lat.back(), lon.back(), la, lo);
      lat.push_back(la);
      lon.push_back(lo);
      dist.push_back(length);
    };
    auto lerp = [&](size_t i, double u, int k) { return TRACK_PTS[2 * i + k] + u * (TRACK_PTS[2 * i + 2 + k] - TRACK_PTS[2 * i + k]); };
    add(lerp(seg, bestU, 0), lerp(seg, bestU, 1));
    for (size_t k = 1; k < n; k++) {  // the outline is closed: its last point repeats the first
      size_t i = (seg + k) % (n - 1);
      add(TRACK_PTS[2 * i], TRACK_PTS[2 * i + 1]);
    }
    add(lat[0], lon[0]);
  }
  void at(double s, double& la, double& lo) const {
    s = fmod(fmod(s, length) + length, length);
    size_t i = std::upper_bound(dist.begin(), dist.end(), s) - dist.begin();
    if (i == 0) i = 1;
    if (i >= dist.size()) i = dist.size() - 1;
    double f = (s - dist[i - 1]) / std::max(1e-9, dist[i] - dist[i - 1]);
    la = lat[i - 1] + f * (lat[i] - lat[i - 1]);
    lo = lon[i - 1] + f * (lon[i] - lon[i - 1]);
  }
};

// Speed along the lap: quick on the straights, slower in the corners
static float speedAt(double s, double pace) { return (float)((118 + 52 * sin(s / 310.0) + 18 * sin(s / 97.0)) * pace); }

int main(int argc, char** argv) {
  outDir = argc > 1 ? argv[1] : ".";
  DisplayManager d;
  d.begin();
  GPSHandler gps;

  Lap1 loop;
  loop.build();
  Track track;
  track.id = "calabogie";
  track.venueId = "calabogie";
  track.name = TRACK_NAME;
  track.location = TRACK_LOCATION;
  track.country = "Canada";
  track.length = TRACK_LENGTH_M;
  track.hasStartFinish = true;
  track.startFinish = {SF_LAT, SF_LON, 15};
  track.splits.push_back({loop.lat[40], loop.lon[40], 15});
  track.splits.push_back({loop.lat[80], loop.lon[80], 15});
  track.hasMap = true;
  std::vector<geo::XYf> outline;
  for (size_t i = 0; i < loop.lat.size(); i++) {
    geo::XY p = geo::toLocal(loop.lat[i], loop.lon[i], SF_LAT, SF_LON);
    outline.push_back({(float)p.x, (float)p.y});
  }
  track.map.push_back(outline);

  // --- start-up, GPS and menus ---
  d.drawBoot("Detecting GPS…");
  save(d, "01_boot");
  setGps(gps, false, 0, 0, 0, 0);
  d.drawGpsWait(gps, 131, "SD", true, false);
  save(d, "02_gps_wait");
  d.setBatteryPercent(76);
  setGps(gps, true, SF_LAT + 0.002, SF_LON, 0, 0);
  d.drawGpsWait(gps, 131, "SD", true, true);
  save(d, "03_gps_status");
  {  // the home screen (9.1): each kind of Next up card, then BOOT navigation
    nextup::Inputs in;
    in.fix = true;
    in.today = "20261025";
    in.circuitIndex = 3;
    in.circuitDistM = 40;
    in.circuitName = "Calabogie";
    in.layoutName = "Full";
    in.pbMs = 163611;
    d.drawHome(nextup::choose(in), -1, gps, true);
    save(d, "04_home_circuit");
    nextup::Inputs course = in;
    course.courseIndex = 7;
    course.courseDistM = 120;
    course.courseEvent = "20261025";
    course.courseName = "Oct 25 course";
    course.runNext = 4;
    course.bestRunMs = 52310;
    d.drawHome(nextup::choose(course), -1, gps, true);
    save(d, "04b_home_course");
    nextup::Inputs drive;
    drive.fix = true;
    drive.today = "20261025";
    d.drawHome(nextup::choose(drive), -1, gps, true);
    save(d, "04c_home_drive");
    setGps(gps, false, 0, 0, 0, 0);
    nextup::Inputs nofix = in;
    nofix.fix = false;
    d.drawHome(nextup::choose(nofix), -1, gps, true);
    save(d, "04d_home_no_fix");
    d.drawHome(nextup::choose(nofix), home::Settings, gps, false);  // BOOT navigation, no fix
    save(d, "05_home_boot_nofix");
    setGps(gps, true, SF_LAT + 0.0004, SF_LON, 0, 0);
    d.drawHome(nextup::choose(in), home::NextUp, gps, false);
    save(d, "05b_home_boot_next_up");
    d.drawHome(nextup::choose(in), home::Autocross, gps, false);
    save(d, "05c_home_boot_tile");
    d.drawHome(nextup::choose(in), home::Wifi, gps, false);
    save(d, "05d_home_boot_wifi");
  }
  setGps(gps, true, SF_LAT + 0.02, SF_LON, 0, 0);
  d.setPersonalBest(163611, 20260914);  // shown on the track list and Ready screens
  d.drawTrackSelect(track, 24, 131, 2210, gps, true);
  save(d, "06_track_select");
  Track layout = track;
  layout.layoutName = "Full";
  d.drawTrackConfirm(layout, 2210, true, true, 0, 3);
  save(d, "07_ready");
  d.drawTrackConfirm(track, 40, true, true, 0, 1, false, true);  // boot pick, parked: starts when the car drives off
  save(d, "07b_ready_autostart");
  d.drawTrackConfirm(layout, 2210, true, true, 0, 3, true, false);  // from the track list: Back
  save(d, "07c_ready_from_list");
  d.setDriverCar("Dylan \xC2\xB7 Civic Type R");  // once there is more than one driver or car (8.5)
  d.drawTrackConfirm(layout, 2210, true, true, 0, 3);
  save(d, "07d_ready_driver_car");
  d.drawTrackSelect(track, 24, 131, 2210, gps, true);
  save(d, "06b_track_select_driver_car");
  d.drawTrackDayExtra(false, 130, 132, gps, true);  // + New circuit, last before the Bench Test
  save(d, "06c_track_day_new_circuit");
  d.drawTrackDayExtra(false, 130, 132, gps, false);
  save(d, "06d_track_day_new_circuit_boot");
  d.drawTrackDayExtra(true, 132, 132, gps, false);  // BOOT only: a Back entry after the Bench Test
  save(d, "06e_track_day_back_boot");
  d.setPersonalBest(0, 0);
  d.drawTrackConfirm(track, 40, true, true, 0, 1, false, false);  // a pair with no PB yet
  save(d, "07e_ready_driver_car_no_pb");
  d.setDriverCar("");
  d.setPersonalBest(163611, 20260914);

  // --- a session: drive laps through the real lap timer ---
  LapTimer timer;
  timer.begin(track.startFinish, track.splits, MIN_LAP_TIME_MS);
  RunningOverlay ov;
  timing::DeltaTrend trend;
  const double paces[] = {0.93, 1.0, 1.03, 0.99, 1.01};
  double s = -150;  // start on the out lap, before the line
  uint32_t gpsMs = 36000000;
  mockMillis = 1000;
  float kmh = 0;
  double la = SF_LAT, lo = SF_LON;
  auto drive = [&](double untilS) {
    while (s < untilS) {
      int lap = (int)floor(s / loop.length) + 1;
      double pace = paces[std::min(std::max(lap, 0), 4)];
      kmh = speedAt(s, pace);
      s += kmh / 3.6 * 0.05;
      gpsMs += 50;
      mockMillis += 50;
      loop.at(s, la, lo);
      // The engine warming lap by lap, for the summary's and debrief's hottest (6.5)
      const float coolC = 84.0f + 4.5f * std::max(lap, 0) + (float)fmod(s, loop.length) / loop.length * 3.0f;
      timer.noteTemps(true, coolC, true, coolC + 24.0f);
      LapEvents evs = timer.onFix(la, lo, kmh, gpsMs, mockMillis);
      if (evs.find(LapEventType::LapCompleted)) trend.clear();
      trend.add(gpsMs, timer.liveDeltaMs(), timer.lapRunning() && timer.liveDeltaMs() != NO_DELTA);
    }
    setGps(gps, true, la, lo, kmh, gpsMs);
  };
  // What main.cpp keeps in the overlay while timing: the trend, and the sector strip's bests
  // (this session's, with a made-up best ever a little faster in some sectors)
  auto timingExtras = [&](RunningOverlay& o) {
    o.trendMs = trend.change();
    o.sectorBestDay.clear();
    for (size_t i = 0; i <= timer.sectorGates().size() && !timer.sectorGates().empty(); i++)
      o.sectorBestDay.push_back(timer.sectorBestMs(i));
    o.sectorBestEver = o.sectorBestDay;
    for (size_t i = 0; i < o.sectorBestEver.size(); i++)
      if (i % 3 == 1 && o.sectorBestEver[i] > 0) o.sectorBestEver[i] -= 400;
  };

  drive(-60);
  d.setSessionClock("1:12", false);  // the session clock in the header (main.cpp sets it each frame)
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "08_running_outlap");
  d.setSessionClock("14:05", false);
  drive(loop.length + 20);  // lap 1 done: corner sectors from it, as the firmware makes them
  {
    std::vector<sectors::PathPt> p;
    for (const LapTimer::TracePt& tp : timer.bestPath()) p.push_back({tp.x, tp.y, tp.t, tp.speedKmh});
    std::vector<TrackPoint> gates;
    for (const sectors::Cut& c : sectors::findCuts(p)) {
      TrackPoint g = {0, 0, 15, c.headingDeg, HeadingFrom::Set};
      geo::fromLocal(c.x, c.y, track.startFinish.lat, track.startFinish.lon, g.lat, g.lon);
      gates.push_back(g);
    }
    timer.setSectors(gates);
  }
  drive(loop.length * 2 + 1900);  // into lap 3, on a faster pace than lap 2
  timingExtras(ov);
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "09_running_ahead");
  mockSettings.deltaColours = DeltaColours::BlueOrange;  // the Delta colours setting
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "09d_running_blue_orange");
  mockSettings.deltaColours = DeltaColours::GreenRed;
  mockSettings.bigNumber = BigNumber::Delta;  // the Big number setting
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "09b_running_big_delta");
  mockSettings.bigNumber = BigNumber::Predicted;
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "09c_running_big_predicted");
  mockSettings.bigNumber = BigNumber::LapTime;
  d.drawRunningMap(track, timer, gps, ov, 0);
  save(d, "10_map");
  drive(loop.length * 4 + 1200);  // lap 5, slower
  timingExtras(ov);
  ov.splitIndex = 0;
  ov.splitMs = 41230;
  ov.deltaMs = 380;
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "11_running_behind_split");
  mockSettings.bigNumber = BigNumber::Delta;
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "11b_running_big_delta_behind");
  mockSettings.bigNumber = BigNumber::LapTime;
  ov.splitIndex = -1;
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "11c_running_behind_strip");
  ov = RunningOverlay();
  const Lap* best = timer.bestLap();
  ov.showLap = true;
  ov.lapNumber = best->number;
  ov.lapMs = best->timeMs;
  ov.lapDeltaMs = -1234;
  ov.lapIsBest = true;
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "12_lap_flash_session_best");
  ov.lapIsDayBest = true;
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "12a_lap_flash_day_best");
  ov.lapIsPb = true;
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "12b_lap_flash_pb");
  ov = RunningOverlay();
  ov.showLap = true;
  ov.lapNumber = 6;
  ov.lapMs = best->timeMs + 830;
  ov.lapDeltaMs = 830;
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "12c_lap_flash_slower");
  ov = RunningOverlay();
  ov.showLap = true;
  ov.lapNumber = best->number;
  ov.lapMs = best->timeMs;
  ov.lapIsBest = ov.lapIsDayBest = true;
  d.drawRunningMap(track, timer, gps, ov, 0);
  save(d, "13_map_lap_flash");
  {  // The slow screen: behind a safety car, then in the pit lane with a session length set
    setGps(gps, true, la, lo, 34, gpsMs);
    d.setSessionClock("7:26 left", false);
    d.drawSlow(track, timer, gps, false, 0);
    save(d, "14a_slow");
    setGps(gps, true, la, lo, 58, gpsMs);
    d.setSessionClock("0:42 left", true);
    d.drawSlow(track, timer, gps, true, 0);
    save(d, "14b_slow_pit_lane");
    d.setSessionClock("14:05", false);
    setGps(gps, true, la, lo, kmh, gpsMs);
  }
  ov = RunningOverlay();
  ov.notice = "Layout: Full";
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "14_layout_notice");
  ov = RunningOverlay();
  btConnected = true;
  gps.simulating = true;
  gps.everFixed = false;
  d.drawRunning(track, timer, gps, ov, 1200);
  save(d, "15_nofix_hold_bench_bt");
  gps.simulating = false;
  btConnected = false;
  setGps(gps, true, la, lo, 96, gpsMs);
  d.drawSetLine(track, gps, 0);
  save(d, "16_set_line");
  d.drawSummary(track, timer, "/sessions/20261019_093012_calabogie.json", 2, false);
  save(d, "17_summary");
  d.drawSummary(track, timer, "/sessions/20261019_093012_calabogie.json", 2, true);
  save(d, "17b_in_the_pits");
  {  // The debrief, worked out as main.cpp's buildDebrief does
    std::vector<debrief::LapIn> laps;
    for (size_t i = 0; i < timer.lapCount(); i++)
      laps.push_back({timer.lap(i).number, timer.lap(i).timeMs, timer.lap(i).tags, timer.lap(i).sectorMs});
    DebriefView dv;
    dv.r = debrief::analyse(laps, timer.sectorGates().size() + 1);
    dv.session = 2;
    dv.lastSession = 1;
    dv.lastBestMs = dv.r.bestMs + 1040;
    dv.lastToday = true;
    dv.allTimeTheoreticalMs = dv.r.theoreticalMs - 420;
    if (dv.r.valid && timer.bestPathLapIndex() == dv.r.bestIndex) {
      const std::vector<int32_t>& g = timer.lap(dv.r.bestIndex).sectorMs;
      for (size_t k = 0; k < dv.r.top.size(); k++) {
        int s = dv.r.top[k];
        if (timer.sectorBestMs(s) != dv.r.best[s].ms) continue;
        int32_t from = s == 0 ? 0 : g[s - 1], to = s < (int)g.size() ? g[s] : (int32_t)dv.r.bestMs;
        dv.hints[k] = debrief::hintText(debrief::hint(debrief::slice(timer.bestPath(), from, to),
                                                      debrief::slice(timer.sectorBestPath(s), INT32_MIN, INT32_MAX)));
      }
    }
    // The made-up laps all have the same shape, so nothing is lost anywhere: show three
    // sectors with made-up losses (as from laps 3 and 4), still adding up
    const int32_t loss[3] = {310, 220, 170};
    const char* hint[3] = {"braked 8 m early", "4 km/h slower at the apex", "3 km/h slower on exit"};
    if (dv.r.valid && dv.r.top.empty() && dv.r.sectors >= 4) {
      const int which[3] = {1, 3, (int)dv.r.sectors - 1};
      for (int k = 0; k < 3; k++) {
        dv.r.best[which[k]].ms -= loss[k];
        dv.r.best[which[k]].lap = k == 1 ? 4 : 3;
        dv.r.gaps[which[k]] += loss[k];
        dv.r.theoreticalMs -= loss[k];
        dv.r.top.push_back(which[k]);
        dv.hints[k] = hint[k];
      }
    }
    printf("render_screens: debrief %s, %zu sectors, top %zu\n", dv.r.valid ? "valid" : "NOT valid", dv.r.sectors,
           dv.r.top.size());
    d.drawDebrief(track, timer, dv, 0);
    save(d, "17c_debrief");
    d.drawDebrief(track, timer, dv, 1);
    save(d, "17d_debrief_laps_sectors");
  }

  // --- Free Roam ---
  FreeRoam fr;
  fr.start(mockMillis);
  for (double r = 0; r < loop.length * 1.6; r += 25) {
    loop.at(r, la, lo);
    fr.onFix(la, lo, 84);
    mockMillis += 1000;
  }
  setGps(gps, true, la, lo, 84, gpsMs);
  d.drawFreeRoam(fr, gps, 0);
  save(d, "18_free_roam");
  d.drawFreeRoamSummary(fr, fr.elapsedMs(mockMillis), "/sessions/20261019_101500_freeroam.json");
  save(d, "19_free_roam_summary");
  mockSettings.units = SpeedUnits::Mph;  // the Speed units setting
  d.drawFreeRoam(fr, gps, 0);
  save(d, "19b_free_roam_mph");
  d.drawGpsWait(gps, 131, "SD", true, true);
  save(d, "19c_gps_status_mph");
  mockSettings.units = SpeedUnits::Kmh;

  // --- Performance: a launch to 205 km/h and a stop, through the real PerfRun ---
  perf::PerfRun pr;
  uint32_t t = 50000000;
  float v = 0;
  double alt = 120;
  for (int i = 0; i < 40; i++) pr.onFix(t += 50, 0, alt);  // stopped: armed
  setGps(gps, true, SF_LAT, SF_LON, 0, t);
  d.drawPerfLive(pr, gps, perf::SpeedUnit::Kmh, "1 ft", 0, true);
  save(d, "20_perf_ready");
  while (v < 205) {
    v += (float)(9.0 * (1.0 - v / 260.0)) * 0.05f * 3.6f;
    pr.onFix(t += 50, v, alt);
    if (fabs(v - 142) < 0.6) {
      setGps(gps, true, SF_LAT, SF_LON, v, t);
      d.drawPerfLive(pr, gps, perf::SpeedUnit::Kmh, "1 ft", 0, true);
      save(d, "21_perf_go");
    }
  }
  while (v > 0) {
    v = std::max(0.0f, v - 9.5f * 0.05f * 3.6f);
    pr.onFix(t += 50, v, alt);
  }
  for (int i = 0; i < 60; i++) pr.onFix(t += 50, 0, alt);
  if (!pr.results().empty()) {
    d.drawPerfCard(pr, 0, perf::SpeedUnit::Kmh, true);
    save(d, "22_perf_card");
    d.drawPerfCard(pr, 0, perf::SpeedUnit::Mph, true);  // what main.cpp passes with the Speed units setting on mph
    save(d, "22b_perf_card_mph");
    d.drawPerfCard(pr, (int)pr.results().size() - 1, perf::SpeedUnit::Kmh, true);
    save(d, "23_perf_card_last");
  }
  d.drawPerfCard(pr, -1, perf::SpeedUnit::Kmh, true);
  save(d, "24_perf_best");

  // --- Wi-Fi, power and firmware ---
  d.drawWifi(true, "TrackRat-3F2A", "192.168.4.1", 1, 4821, -1, false, true);
  save(d, "25_wifi");
  d.drawWifi(true, "TrackRat-3F2A", "192.168.4.1", 1, 4821, 63, false, true);
  save(d, "26_wifi_installing");
  d.drawMessage("BATTERY LOW", "Saved. Going to sleep.", "Charge it, then touch or press BOOT", ui::C_YELLOW);
  save(d, "27_battery_low");
  d.drawPowerHold(POWER_OFF_BAR_MS + (POWER_OFF_HOLD_MS - POWER_OFF_BAR_MS) * 2 / 3);  // BOOT held 2.5 s
  save(d, "27b_power_off_hold");
  d.drawMessage("POWER OFF", "Switching off…", "Tap the screen to turn it back on", ui::C_ORANGE);
  save(d, "27c_powering_off");
  d.drawFirmwarePrompt("2026-10-18 21:40", 912384, true);
  save(d, "28_firmware_prompt");
  d.drawFirmwarePrompt("2026-10-18 21:40", 912384, false);
  save(d, "28b_firmware_prompt_boot_only");
  d.drawFirmwareProgress(42);
  save(d, "29_firmware_progress");

  // --- Settings and the first-boot questions ---
  SettingsData sd = mockSettings;
  sd.utcOffsetMin = -240;
  sd.brightness = 70;
  sd.utcOffsetMin = 330;
  sd.bluetooth = true;
  const int64_t localNow = (int64_t)(14 * 3600 + 5 * 60) * 1000;
  // Drivers and cars (8.5): three drivers and four cars, the first of each a name of its own
  profiles::Book people;
  people.rename(profiles::Kind::Driver, 1, "Dylan");
  people.add(profiles::Kind::Driver, "Sam");
  people.add(profiles::Kind::Driver, "Alex");
  people.rename(profiles::Kind::Car, 1, "Civic Type R");
  people.add(profiles::Kind::Car, "Miata");
  people.add(profiles::Kind::Car, "Rental kart");
  people.add(profiles::Kind::Car, "Club Corolla");
  people.select(profiles::Kind::Driver, 2);
  people.select(profiles::Kind::Car, 2);
  SettingsScreenState ui;
  ui.begin(true);
  ui.setPeople(people);
  d.drawSettings(sd, ui, true, localNow);
  save(d, "30_settings_list");
  ui.drag(-150);  // a finger dragged up part way
  d.drawSettings(sd, ui, true, localNow);
  save(d, "30b_settings_list_scrolled");
  ui.drag(-1000);  // and to the end
  ui.dragEnd();
  d.drawSettings(sd, ui, true, localNow);
  save(d, "30c_settings_list_bottom");
  SettingsScreenState boot;
  boot.begin(false);  // no touch: BOOT highlight, hint at the bottom
  boot.setPeople(people);
  SettingsData scratch = sd;
  for (int i = 0; i < 3; i++) boot.handle(Key::BootShort, scratch);
  d.drawSettings(sd, boot, false, localNow);
  save(d, "31_settings_list_boot");
  SettingsScreenState driverPage;  // the Driver page by touch, then the Car page by BOOT (eight names would scroll)
  driverPage.begin(true);
  driverPage.setPeople(people);
  driverPage.tap(100, driverPage.rowScreenY(SettingsScreenState::rowOfItem(SettingItem::Driver)) + 10, scratch);
  d.drawSettings(sd, driverPage, true, localNow);
  save(d, "31b_settings_driver");
  SettingsScreenState carPage;
  carPage.begin(false);
  carPage.setPeople(people);
  carPage.handle(Key::BootShort, scratch);
  carPage.handle(Key::BootLong, scratch);
  carPage.handle(Key::BootShort, scratch);
  d.drawSettings(sd, carPage, false, localNow);
  save(d, "31c_settings_car_boot");
  profiles::Book many = people;  // a full list: it scrolls under the top bar
  for (int i = 0; many.cars.count < 8; i++) {
    char n[24];
    snprintf(n, sizeof(n), "Car %d", i + 5);
    many.add(profiles::Kind::Car, n);
  }
  SettingsScreenState fullPage;
  fullPage.begin(true);
  fullPage.setPeople(many);
  fullPage.tap(100, fullPage.rowScreenY(SettingsScreenState::rowOfItem(SettingItem::Car)) + 10, scratch);
  fullPage.drag(-90);
  d.drawSettings(sd, fullPage, true, localNow);
  save(d, "31d_settings_car_eight");
  SettingsScreenState page;  // the Speed units page, then the GPS mode page (it has a long note)
  page.begin(true);
  page.tap(100, page.rowScreenY(SettingsScreenState::rowOfItem(SettingItem::Units)) + 10, scratch);
  d.drawSettings(sd, page, true, localNow);
  save(d, "32_settings_choice");
  SettingsScreenState gpsPage;
  gpsPage.begin(false);
  for (int i = 0; i < SettingsScreenState::rowOfItem(SettingItem::Gps); i++) gpsPage.handle(Key::BootShort, scratch);
  gpsPage.handle(Key::BootLong, scratch);
  gpsPage.handle(Key::BootShort, scratch);
  d.drawSettings(sd, gpsPage, false, localNow);
  save(d, "32b_settings_choice_boot");
  SettingsScreenState level;
  level.begin(true);
  level.tap(100, level.rowScreenY(2) + 10, scratch);
  d.drawSettings(sd, level, true, localNow);
  save(d, "33_settings_brightness");
  SettingsScreenState zone;
  zone.begin(true);
  const int zoneRow = SettingsScreenState::rowOfItem(SettingItem::TimeZone);
  zone.tap(100, zone.rowScreenY(zoneRow) + 10, scratch);  // scrolled out of view: nothing opens
  zone.setScroll(1000);
  zone.tap(100, zone.rowScreenY(zoneRow) + 10, scratch);
  d.drawSettings(sd, zone, true, localNow);
  save(d, "34_settings_time_zone");
  d.drawSettings(sd, zone, false, localNow);
  save(d, "35_settings_time_zone_boot_only");

  // Settings > Engine data (6.1): nothing chosen, connected and reading, and the Find adapter list
  DeviceScreenState eng;
  eng.begin(true);
  d.drawEngineData(eng, obdStatus, nullptr, 0, false, true);
  save(d, "36_engine_data_none");
  obdStatus.configured = obdStatus.connected = true;
  snprintf(obdStatus.name, sizeof(obdStatus.name), "VEEPEAK");
  snprintf(obdStatus.address, sizeof(obdStatus.address), "c4:be:84:12:34:56");
  snprintf(obdStatus.protocol, sizeof(obdStatus.protocol), "AUTO, ISO 15765-4 (CAN 11/500)");
  obdStatus.linked = true;
  obdStatus.phase = elm::Client::Phase::Running;
  obdStatus.suffix = obdStatus.multi = true;
  obdStatus.stats.requestHz = 21.4f;
  obdStatus.stats.rpmHz = obdStatus.stats.throttleHz = 19.3f;
  obdStatus.stats.coolantHz = obdStatus.stats.intakeHz = 1.0f;
  d.drawEngineData(eng, obdStatus, nullptr, 0, false, true);
  save(d, "36b_engine_data_reading");
  ObdBle::Found found[3] = {{"VEEPEAK", "c4:be:84:12:34:56", 0, -58}, {"OBDII", "00:1d:a5:68:98:8b", 0, -77}, {"IOS-Vlink", "d0:39:72:aa:bb:cc", 1, -85}};
  eng.handle(Key::BootShort, 3);
  eng.tap(20, DeviceScreenState::BTN_Y + 5, 0);
  d.drawEngineData(eng, obdStatus, found, 3, false, false);
  save(d, "36c_engine_data_find");

  // Settings > Camera (6.2): pairing (the code to match), then connected and recording
  DeviceScreenState cam;
  cam.begin(true);
  camStatus.configured = camStatus.linked = true;
  snprintf(camStatus.name, sizeof(camStatus.name), "OsmoAction4-1A2B");
  snprintf(camStatus.address, sizeof(camStatus.address), "60:60:1f:aa:bb:cc");
  camStatus.phase = dji::Remote::Phase::Waiting;
  camStatus.pairing = true;
  camStatus.code = 4821;
  d.drawCamera(cam, camStatus, true, nullptr, 0, false, true);
  save(d, "37_camera_pairing");
  camStatus.phase = dji::Remote::Phase::Connected;
  camStatus.connected = true;
  camStatus.cam.known = true;
  camStatus.cam.status = 0x03;
  camStatus.cam.battery = 78;
  camStatus.gpsSent = 5123;
  d.drawCamera(cam, camStatus, true, nullptr, 0, false, true);
  save(d, "37b_camera_recording");
  CameraBle::Found cams[1] = {{"OsmoAction4-1A2B", "60:60:1f:aa:bb:cc", 0, -61}};
  cam.tap(20, DeviceScreenState::BTN_Y + 5, 0);
  d.drawCamera(cam, camStatus, true, cams, 1, false, true);
  save(d, "37c_camera_pair_list");

  // The SYNC flash (6.4), for lining a video up with the log
  d.drawSync("14:03:12.3");
  save(d, "38_sync_flash");

  // Coolant (6.5): the top bar's tag in each band, the overheat warning, the Coolant page
  {
    RunningOverlay o;
    obdStatus.configured = obdStatus.connected = true;
    coolantKnown = true;
    const struct { float c; const char* name; } bands[] = {{55, "45_coolant_tag_cold"}, {92, "45b_coolant_tag_normal"},
                                                           {104, "45c_coolant_tag_warm"}, {112, "45d_coolant_tag_hot"}};
    for (auto& b : bands) {
      obdValues.coolantC = b.c;
      const uint32_t keep = mockMillis;
      mockMillis -= mockMillis % (2 * CAM_REC_BLINK_MS);  // the hot tag's "on" half
      d.drawRunning(track, timer, gps, o, 0);
      save(d, b.name);
      mockMillis = keep;
    }
    obdValues.coolantC = 81;  // a warm engine: Free Roam and Performance show it in the top bar too
    d.drawFreeRoam(fr, gps, 0);
    save(d, "45f_coolant_free_roam");
    d.drawPerfLive(pr, gps, perf::SpeedUnit::Kmh, "1 ft", 0, true);
    save(d, "45g_coolant_perf");
    coolantKnown = false;
    d.drawRunning(track, timer, gps, o, 0);
    save(d, "45e_obd_tag_no_coolant_yet");
    obdStatus.connected = false;
    d.drawTempAlert(false, 112, true);
    save(d, "46_overheat_coolant");
    d.drawTempAlert(false, 112, false);
    save(d, "46a_overheat_coolant_yellow");
    mockSettings.units = SpeedUnits::Mph;
    d.drawTempAlert(true, 131, true);
    save(d, "46b_overheat_oil_mph");
    mockSettings.units = SpeedUnits::Kmh;
    SettingsScreenState cool;
    cool.begin(true);
    const int coolRow = SettingsScreenState::rowOfItem(SettingItem::Coolant);
    cool.setScroll(SettingsScreenState::rowTop(coolRow) - 40);
    d.drawSettings(sd, cool, true, localNow);
    save(d, "47_settings_list_coolant_row");
    cool.tap(100, cool.rowScreenY(coolRow) + 10, scratch);
    d.drawSettings(sd, cool, true, localNow);
    save(d, "47b_settings_coolant");
    SettingsScreenState coolBoot;
    coolBoot.begin(false);
    while (coolBoot.highlight != coolRow) coolBoot.handle(Key::BootShort, scratch);
    coolBoot.handle(Key::BootLong, scratch);
    coolBoot.handle(Key::BootLong, scratch);
    SettingsData f = sd;
    f.units = SpeedUnits::Mph;
    d.drawSettings(f, coolBoot, false, localNow);
    save(d, "47c_settings_coolant_boot_mph");
  }
  {  // Settings > Power off: it asks first
    SettingsScreenState end;  // the bottom of the list: Back to menu and the firmware version
    end.begin(true);
    end.setScroll(1000);
    d.drawSettings(sd, end, true, localNow);
    save(d, "47d_settings_list_end_firmware");
    SettingsScreenState off;
    off.begin(true);
    const int offRow = SettingsScreenState::rowOfItem(SettingItem::PowerOff);
    off.tap(100, off.rowScreenY(offRow) + 10, scratch);  // scrolled out of view: nothing opens
    off.setScroll(1000);
    off.tap(100, off.rowScreenY(offRow) + 10, scratch);
    d.drawSettings(sd, off, true, localNow);
    save(d, "48_settings_power_off");
    d.drawSettings(sd, off, false, localNow);
    save(d, "48b_settings_power_off_boot_only");
  }
  d.drawSetup(0, 0, true);
  save(d, "40_setup_which_way_up");
  d.drawSetup(0, 1, false);
  save(d, "41_setup_which_way_up_boot");
  d.drawSetup(1, 0, true);
  save(d, "42_setup_units");
  d.drawSetup(1, 1, false);
  save(d, "43_setup_units_boot");
  setGps(gps, true, SF_LAT + 0.002, SF_LON, 0, 0);

  // --- Autocross: courses as events (Courses.h) ---
  {
    setGps(gps, true, SF_LAT + 0.002, SF_LON, 0, 0);
    AutocrossView hub;
    hub.gpsFix = true;
    hub.haveCourse = true;
    hub.name = "Oct 25 course";
    hub.today = true;
    const uint32_t times[] = {53400, 52310, 53020, 51980, 52840, 54210, 55020, 52500, 52390};
    for (uint32_t t : times) hub.runs.push_back({t, 0});
    hub.runs[5].tags = laptags::PIT;
    hub.runs[3].tags = laptags::PIT;  // faster, but not clean: never the best
    hub.nextRun = (int)hub.runs.size() + 1;
    courses::Day day;
    for (const courses::Run& r : hub.runs) day.add(r);
    hub.best = day.best();
    hub.last = day.last();
    d.drawAutocross(hub, gps, -1, true);
    save(d, "45_autocross_hub");
    d.drawAutocross(hub, gps, 1, false);  // BOOT navigation
    save(d, "45b_autocross_hub_boot");
    AutocrossView few = hub;
    few.runs.resize(3);
    few.nextRun = 4;
    few.best = 1;
    few.last = 2;
    d.drawAutocross(few, gps, -1, true);
    save(d, "45c_autocross_hub_three_runs");
    AutocrossView old = few;
    old.today = false;
    old.dateText = "Oct 20";
    old.name = "Oct 20 course";
    d.drawAutocross(old, gps, -1, true);
    save(d, "45d_autocross_hub_older_course");
    AutocrossView nofinish = few;  // the finish is set on the first run: until then Adjust finish waits
    nofinish.adjustReason[1] = "set finish first";
    d.drawAutocross(nofinish, gps, -1, true);
    save(d, "45e_autocross_hub_no_finish");
    AutocrossView busy = few;  // the runs are being timed again after a line was saved
    busy.retiming = true;
    busy.adjustReason[0] = busy.adjustReason[1] = "re-timing...";
    d.drawAutocross(busy, gps, -1, true);
    save(d, "45f_autocross_hub_retiming");
    AutocrossView none;
    none.gpsFix = true;
    none.adjustReason[0] = none.adjustReason[1] = "needs a run";
    d.drawAutocross(none, gps, -1, true);
    save(d, "46_autocross_hub_no_course");
    none.gpsFix = false;
    setGps(gps, false, 0, 0, 0, 0);
    d.drawAutocross(none, gps, 4, false);
    save(d, "46b_autocross_hub_no_course_no_fix");

    setGps(gps, true, SF_LAT + 0.002, SF_LON, 0, 0);
    d.drawNewCourse(gps, true, -1, true);
    save(d, "47_new_course");
    setGps(gps, true, SF_LAT + 0.002, SF_LON, 23, 0);
    d.drawNewCourse(gps, false, 1, false);
    save(d, "47b_new_course_moving_boot");

    // --- Adjust finish / Adjust start (AdjustLine.h): runs made up from straight passes at 80 km/h ---
    {
      using namespace adjust;
      // A run past a line at the origin heading east, at a steady 80 km/h with a wander of `lateral` metres; the run
      // took `runMs` to get there. Rolling: it began at the line `runMs` ago; standing: it moved off from 0.4 m behind it.
      auto makeRun = [&](int number, Which w, double lateral, int64_t runMs, bool standing) {
        RunData r;
        r.number = number;
        r.minMs = STAGE_MIN_RUN_MS;
        const double v = 80 / 3.6;
        const int64_t cross = 1000000 + number * 200000;  // GPS time of the crossing of the line being moved
        for (int i = 0; i < 100; i++) {
          linenudge::Fix f;
          f.x = -40 + i * v * 0.05 + (standing ? number * 0.9 - 2 : 0);
          f.y = lateral + 0.7 * sin(i * 0.11 + number);
          f.ms = cross + (int64_t)((f.x - 0.0) / v * 1000);
          f.kmh = 80;
          f.joined = i > 0;
          r.near.push_back(f);
        }
        if (w == Which::Finish) {
          r.fixedMs = cross - runMs;  // the start crossing
        } else if (!standing) {
          r.fixedMs = cross + runMs;  // the finish crossing
        } else {
          r.hasStood = true;
          r.stood.x = -0.4;
          r.stood.y = 0;
          r.stood.kmh = 0;
          r.moveOffMs = cross - 1700;
          r.fixedMs = r.moveOffMs + runMs;
          // (the old time is the run's from moving off: the car's fixes begin after it)
        }
        return r;
      };
      auto line = [&](double heading) {
        TrackPoint p = {45.0, -75.0, 10, (float)heading, HeadingFrom::Set};
        return p;
      };
      std::vector<RunData> three;
      three.push_back(makeRun(1, Which::Finish, 0.6, 53400, false));
      three.push_back(makeRun(2, Which::Finish, -0.5, 52310, false));
      three.push_back(makeRun(3, Which::Finish, 1.1, 53020, false));
      Model fin;
      fin.setup(Which::Finish, line(90), 45.0, -75.0, std::move(three));
      fin.nudge(3);  // +1.5 m
      View v;
      fin.fillView(v);
      d.drawAdjust(v, -1, true);
      save(d, "49_adjust_finish_moved");
      d.drawAdjust(v, BTN_LATER, false);
      save(d, "49b_adjust_finish_boot");

      Model nine;
      std::vector<RunData> many;
      const int64_t t9[] = {53400, 52310, 53020, 51980, 52840, 54210, 55020, 52500, 52390};
      for (int i = 0; i < 9; i++) many.push_back(makeRun(i + 1, Which::Finish, (i % 4 - 1.5) * 0.7, t9[i], false));
      nine.setup(Which::Finish, line(90), 45.0, -75.0, std::move(many));
      nine.nudge(-4);
      View v9;
      nine.fillView(v9);
      d.drawAdjust(v9, -1, true);
      save(d, "49c_adjust_finish_nine_runs");

      std::vector<RunData> st;
      st.push_back(makeRun(1, Which::Start, 0.3, 53400, true));
      st.push_back(makeRun(2, Which::Start, -0.4, 52310, true));
      st.push_back(makeRun(3, Which::Start, 0.9, 53020, false));
      Model start;
      start.setup(Which::Start, line(90), 45.0, -75.0, std::move(st));
      start.nudge(4);  // +2 m: ahead of the car, so the standing runs start at the line
      View vs;
      start.fillView(vs);
      d.drawAdjust(vs, -1, true);
      save(d, "50_adjust_start_standing_note");

      View reading;
      reading.finish = true;
      reading.phase = Phase::Reading;
      reading.done = 2;
      reading.total = 4;
      reading.message = "Log 2 of 4. Only the fixes near the line are kept.";
      d.drawAdjust(reading, -1, true);
      save(d, "51_adjust_reading_runs");
      View saving = reading;
      saving.phase = Phase::Saving;
      saving.message = "The line is saved. Timing the day's sessions again.";
      d.drawAdjust(saving, BTN_CANCEL, false);
      save(d, "51b_adjust_retiming");
      View none;
      none.phase = Phase::NoLogs;
      none.message = "No session logs for this course's day. Turn on full logging, with an SD card.";
      d.drawAdjust(none, -1, true);
      save(d, "51c_adjust_no_logs");
    }

    // The timing screen waiting for run 4: armed at the start of a course, then the finish tap's Undo
    LapTimer stage;
    stage.begin({SF_LAT, SF_LON, 15}, {}, STAGE_MIN_RUN_MS);
    stage.setStage({SF_LAT + 0.004, SF_LON, 15}, true);
    uint32_t t0 = 40000000;
    for (int i = 0; i < 30; i++) stage.onFix(SF_LAT, SF_LON, 0, t0 + i * 50, 5000 + i * 50);
    setGps(gps, true, SF_LAT, SF_LON, 0, t0 + 1500);
    RunningOverlay sov;
    d.setSessionClock("", false);
    d.setRunBase(3);
    d.drawRunning(track, stage, gps, sov, 0);
    save(d, "48c_timing_run_armed");
    d.setFinishUndo(0, true);
    // Moving off, the run under way; then the first run's finish tap
    LapTimer first;
    first.begin({SF_LAT, SF_LON, 15}, {}, STAGE_MIN_RUN_MS);
    first.setStage({0, 0, 15}, false);
    for (int i = 0; i < 30; i++) first.onFix(SF_LAT, SF_LON, 0, t0 + i * 50, 5000 + i * 50);  // armed at the start
    for (int i = 30; i < 1230; i++) first.onFix(SF_LAT + (i - 30) * 0.000004, SF_LON, 55, t0 + i * 50, 5000 + i * 50);
    first.finishRunNow(SF_LAT + 1200 * 0.000004, SF_LON, t0 + 1230 * 50);
    d.setRunBase(0);
    RunningOverlay fov;
    fov.showLap = true;
    fov.isRun = true;
    fov.lapNumber = 1;
    fov.lapMs = first.lastLap() ? first.lastLap()->timeMs : 0;
    fov.lapIsBest = true;
    d.setFinishUndo(3400, true);
    setGps(gps, true, SF_LAT + 0.004, SF_LON, 38, t0 + 20000);
    d.drawRunning(track, first, gps, fov, 0);
    save(d, "48d_finish_undo");
    d.setFinishUndo(2000, false);
    d.drawRunning(track, first, gps, fov, 0);
    save(d, "48e_finish_undo_boot");
    d.setFinishUndo(0, true);
    d.setRunBase(0);
  }
  printf("render_screens: laps %d, best %s\n", (int)timer.lapCount(), formatLapTime(timer.bestLap()->timeMs).c_str());
  return 0;
}
