#pragma once
// The stand-ins for the firmware that the screen drivers share: screens.cpp and demo.cpp.
// Each is its own program, so definitions in this header are fine.
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
#include "TimeFormat.h"
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
  char buf[24];
  timefmt::lapTime(buf, sizeof(buf), ms, decimals);
  return String(buf);
}
String formatDelta(int32_t ms) {
  char buf[16];
  timefmt::delta(buf, sizeof(buf), ms);
  return String(buf);
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
