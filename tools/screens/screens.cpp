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
#include "Settings.h"
#include "Storage.h"
#include "Sectors.h"
#include "UiTheme.h"
#include "track_data.h"  // TRACK_NAME, TRACK_LOCATION, TRACK_LENGTH_M, SF_LAT, SF_LON, TRACK_PTS (lat, lon pairs)

uint32_t mockMillis = 0;
static bool btConnected = false;

// --- stand-ins for the parts of the firmware the screens call ---------------
bool GPSHandler::hasFix() const { return everFixed; }
bool GPSHandler::receivingData() const { return simulating || lastSentenceMillis != 0; }
bool DisplayManager::splashRunning() const { return false; }
const char* Storage::sessionFSName() { return "SD"; }
bool RaceBoxBle::connected() { return btConnected; }
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
  d.drawMenu(-1, gps, true, true);
  save(d, "04_menu");
  setGps(gps, false, 0, 0, 0, 0);
  d.drawMenu(5, gps, false, false);  // BOOT navigation, Wi-Fi highlighted, no fix
  save(d, "05_menu_boot_nofix");
  setGps(gps, true, SF_LAT + 0.02, SF_LON, 0, 0);
  d.setPersonalBest(102345, 20260914);  // shown on the track list and Ready screens
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

  // --- a session: drive laps through the real lap timer ---
  LapTimer timer;
  timer.begin(track.startFinish, track.splits, MIN_LAP_TIME_MS);
  RunningOverlay ov;
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
      timer.onFix(la, lo, kmh, gpsMs, mockMillis);
    }
    setGps(gps, true, la, lo, kmh, gpsMs);
  };

  drive(-60);
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "08_running_outlap");
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
  drive(loop.length * 2 + 900);  // into lap 3, on a faster pace than lap 2
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "09_running_ahead");
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
  ov.splitIndex = 0;
  ov.splitMs = 41230;
  ov.deltaMs = 380;
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "11_running_behind_split");
  mockSettings.bigNumber = BigNumber::Delta;
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "11b_running_big_delta_behind");
  mockSettings.bigNumber = BigNumber::LapTime;
  ov = RunningOverlay();
  const Lap* best = timer.bestLap();
  ov.showLap = true;
  ov.lapNumber = best->number;
  ov.lapMs = best->timeMs;
  ov.lapIsBest = true;
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "12_lap_flash_best");
  ov.lapIsPb = true;
  d.drawRunning(track, timer, gps, ov, 0);
  save(d, "12b_lap_flash_pb");
  ov.lapIsPb = false;
  d.drawRunningMap(track, timer, gps, ov, 0);
  save(d, "13_map_lap_flash");
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
  d.drawFirmwarePrompt("2026-10-18 21:40", 912384, true);
  save(d, "28_firmware_prompt");
  d.drawFirmwareProgress(42);
  save(d, "29_firmware_progress");

  // --- Settings and the first-boot questions ---
  SettingsData sd = mockSettings;
  sd.utcOffsetMin = -240;
  sd.brightness = 70;
  sd.utcOffsetMin = 330;
  sd.bluetooth = true;
  const int64_t localNow = (int64_t)(14 * 3600 + 5 * 60) * 1000;
  SettingsScreenState ui;
  ui.begin(true);
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
  SettingsData scratch = sd;
  for (int i = 0; i < 3; i++) boot.handle(Key::BootShort, scratch);
  d.drawSettings(sd, boot, false, localNow);
  save(d, "31_settings_list_boot");
  SettingsScreenState page;  // the Speed units page, then the GPS mode page (it has a long note)
  page.begin(true);
  page.tap(100, page.rowScreenY(0) + 10, scratch);
  d.drawSettings(sd, page, true, localNow);
  save(d, "32_settings_choice");
  SettingsScreenState gpsPage;
  gpsPage.begin(false);
  for (int i = 0; i < 5; i++) gpsPage.handle(Key::BootShort, scratch);
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
  zone.tap(100, zone.rowScreenY(8) + 10, scratch);  // scrolled out of view: nothing opens
  zone.setScroll(1000);
  zone.tap(100, zone.rowScreenY(8) + 10, scratch);
  d.drawSettings(sd, zone, true, localNow);
  save(d, "34_settings_time_zone");
  d.drawSettings(sd, zone, false, localNow);
  save(d, "35_settings_time_zone_boot_only");
  d.drawSetup(0, 0, true);
  save(d, "40_setup_which_way_up");
  d.drawSetup(0, 1, false);
  save(d, "41_setup_which_way_up_boot");
  d.drawSetup(1, 0, true);
  save(d, "42_setup_units");
  d.drawSetup(1, 1, false);
  save(d, "43_setup_units_boot");
  setGps(gps, true, SF_LAT + 0.002, SF_LON, 0, 0);
  d.drawMenu(4, gps, false, false);  // BOOT navigation, Settings highlighted
  save(d, "44_menu_settings_highlighted");
  printf("render_screens: laps %d, best %s\n", (int)timer.lapCount(), formatLapTime(timer.bestLap()->timeMs).c_str());
  return 0;
}
