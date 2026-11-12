// Stand-in for src/GPSHandler.cpp (tools/build_sim.py): no UART. A fix comes from JavaScript (sim::gpsFix) and
// goes in the queue the way a NAV-PVT message does; the Bench Test's simulated car and the Drag launch demo are
// the firmware's own code, copied. Same header (include/GPSHandler.h).
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <Arduino.h>
#include "GpsFix.h"
#include "GpsSource.h"
#include "SettingsData.h"
#include "UbxParser.h"
#include <TinyGPSPlus.h>
#define private public  // (to put a fix in the queue from outside)
#include "GPSHandler.h"
#undef private
#include "Config.h"
#include "LocalTime.h"
#include "SimTrack.h"
#include "Settings.h"
#include "sim.h"

extern GPSHandler gps;

void GPSHandler::wake() {}

void GPSHandler::begin() {
  systems = settings::gpsSystems(Settings::get().gpsMode);
  const uint32_t pvtLostMs = 3000 / systems.rateHz > 1000 ? 3000 / systems.rateHz : 1000;
  source = GpsSourceSwitch(GPS_USE_UBX, GPS_PVT_WAIT_MS, pvtLostMs);
  if (fixQueue == fixQueueFallback) {
    fixQueue = new GpsFix[FIX_QUEUE_LEN];
    queueCap = FIX_QUEUE_LEN;
    queueHead = queueCount = 0;
  }
  detected = true;
  rxPin = 44;
  txPin = 43;
  baudRate = GPS_DEFAULT_BAUD;
  DEBUG_PRINTF("GPS: simulated, fixes come from the page\n");
}

void GPSHandler::standby() {}

// Bench test: replace the GPS with a simulated car (see SimTrack.h).
// Switching either way clears the position so the two never mix.
void GPSHandler::setSimulated(bool on, SimMode mode) {
  if (on == simulating && (!on || mode == simMode)) return;
  simulating = on;
  simMode = mode;
  simPhase = 0;
  simPhaseT = simV = simDist = 0;
  simClockMs = 0;
  last = {};
  queueHead = queueCount = 0;
  everFixed = false;
  lastTimeValue = 0xFFFFFFFF;
  sats = 0;
  hdopVal = 99.9f;
  rateHz = 0;
  rateWindowFixes = 0;
  simLastStep = 0;
  simAngle = 0;
  DEBUG_PRINTF("GPS: %s\n", !on ? "back to the real GPS"
                                : mode == SimMode::Launch ? "BENCH TEST - simulated launches" : "BENCH TEST - simulated car");
}

void GPSHandler::update() {
  uint32_t now = millis();

  if (simulating) {
    if (simMode == SimMode::Launch) simulateLaunchStep();
    else simulateStep();
  }

  if (now - rateWindowStart > 3000) {  // decay the rate if fixes stop
    rateHz = 0;
    rateWindowStart = now;
    rateWindowFixes = 0;
  }
}

void GPSHandler::countFixForRate(uint32_t now) {
  if (rateWindowFixes == 0) rateWindowStart = now;
  rateWindowFixes++;
  uint32_t span = now - rateWindowStart;
  if (span >= 2000) {
    rateHz = (rateWindowFixes - 1) * 1000.0f / span;
    rateWindowStart = now;
    rateWindowFixes = 1;
  }
}

// Fixes are queued so none are lost if the main loop is briefly busy
void GPSHandler::queueFix(const GpsFix& f) {
  if (queueCount == queueCap) {  // full: drop the oldest
    queueHead = (queueHead + 1) % queueCap;
    queueCount--;
    droppedFixes++;
  }
  fixQueue[(queueHead + queueCount) % queueCap] = f;
  queueCount++;
}

bool GPSHandler::takeFix(GpsFix& out) {
  if (queueCount == 0) return false;
  out = fixQueue[queueHead];
  queueHead = (queueHead + 1) % queueCap;
  queueCount--;
  return true;
}

bool GPSHandler::hasFix() const {
  return everFixed && (millis() - last.rxMillis) < GPS_FIX_TIMEOUT_MS && sats >= GPS_MIN_SATELLITES;
}

bool GPSHandler::receivingData() const {
  if (simulating) return true;
  return lastSentenceMillis != 0 && (millis() - lastSentenceMillis) < 2000;
}

String GPSHandler::timestampString() {
  char buf[24];
  if (simulating) {
    snprintf(buf, sizeof(buf), "SIM_%lu", (unsigned long)(millis() / 1000));
    return String(buf);
  }
  if (!pvtTimeValid) return String();
  localtime_::stamp(pvtTime.year, pvtTime.month, pvtTime.day, pvtTime.hour, pvtTime.minute, pvtTime.second, utcOffsetMin,
                    buf, sizeof(buf));
  return String(buf);
}

// A car driving anticlockwise around a circle at 10 Hz. Speed varies around
// the lap and from lap to lap so best-lap and delta logic get exercised.
// Fix times come from simClockMs (100 ms per step), not millis(), so a busy
// loop that runs several steps at once still gives each fix its own time.
void GPSHandler::simulateStep() {
  uint32_t now = millis();
  if (simLastStep == 0) simLastStep = now;
  if (now - simLastStep < 100) return;
  simLastStep += 100;
  simClockMs += 100;

  int lapNo = (int)(simAngle / (2 * M_PI));
  float lapFactor = 1.0f + 0.04f * sinf(lapNo * 1.7f);  // laps differ by a few %
  float speedMs = (24.0f + 8.0f * sinf((float)simAngle * 2.0f)) * lapFactor;
  simAngle += speedMs * 0.1 / SIM_RADIUS_M;

  // Start just before the S/F point (angle 0) so the first crossing is quick.
  double a = simAngle - 0.3;
  double dLat = (SIM_RADIUS_M * sin(a)) / 111320.0;
  double dLon = (SIM_RADIUS_M * (cos(a) - 1.0)) / (111320.0 * cos(SIM_SF_LAT * DEG_TO_RAD));

  last.lat = SIM_SF_LAT + dLat;
  last.lon = SIM_SF_LON + dLon;
  last.speedKmh = speedMs * 3.6f;
  // Moving anticlockwise: direction of travel is (-sin a, cos a) east/north
  float course = atan2f(-sinf((float)a), cosf((float)a)) * RAD_TO_DEG;
  last.courseDeg = course < 0 ? course + 360.0f : course;
  last.courseValid = true;
  last.gpsTimeMs = (simClockMs + 12UL * 3600000UL) % 86400000UL;
  publishSimFix(now);
}

// The rest of a simulated fix (a good 3D fix), then on to takeFix() like a real one.
void GPSHandler::publishSimFix(uint32_t now) {
  last.altM = 100.0f;
  last.year = 2026;
  last.month = 1;
  last.day = 1;
  last.rxMillis = now;
  sats = 12;
  hdopVal = 0.8f;
  last.hAccM = 0.9f;
  last.vAccM = 1.5f;
  last.speedAccMs = 0.15f;
  last.headingAccDeg = 0.5f;
  last.vertSpeedMs = 0;
  last.pdop = hdopVal;
  last.fixType = 3;
  last.sats = sats;
  queueFix(last);
  everFixed = true;
  countFixForRate(now);
}

// Performance bench test: a car going north along a straight road. It stands for 4 s, pulls away
// like a quick car (about 4.4 s to 100 km/h, 14 s to 200), brakes hard from 215 km/h to a stop,
// and repeats. 20 fixes a second on its own clock, so the numbers don't depend on how busy the
// main loop is.
void GPSHandler::simulateLaunchStep() {
  uint32_t now = millis();
  if (simLastStep == 0) simLastStep = now;
  if (now - simLastStep < 50) return;
  simLastStep += 50;

  const double dt = 0.05, vmax = 70.0, a0 = 8.0, vEnd = 215 / 3.6, brake = 9.5;
  simPhaseT += dt;
  if (simPhase == 0) {
    simV = 0;
    if (simPhaseT >= 4.0) simPhase = 1, simPhaseT = 0;
  } else if (simPhase == 1) {
    simV = vmax * (1 - exp(-a0 * simPhaseT / vmax));
    if (simV >= vEnd) simPhase = 2, simPhaseT = 0;
  } else {
    simV -= brake * dt;
    if (simV <= 0) simV = 0, simPhase = 0, simPhaseT = 0;
  }
  simDist += simV * dt;
  simClockMs += 50;

  last.lat = SIM_SF_LAT + simDist / 111320.0;
  last.lon = SIM_SF_LON;
  last.speedKmh = simV * 3.6;
  last.courseDeg = 0;
  last.courseValid = simV > 0.5;
  last.gpsTimeMs = (simClockMs + 12UL * 3600000UL) % 86400000UL;
  publishSimFix(now);
}

// ---------------------------------------------------------------------------
// Fixes from JavaScript

namespace sim {

// Days since 1970-01-01 to a calendar date (Howard Hinnant's civil_from_days)
static void civil(int64_t z, int& y, int& m, int& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yoe + era * 400 + (m <= 2));
}

void gpsFix(double lat, double lon, double speedKmh, double headingDeg, double accuracyM, double utcMs, int satellites) {
  GPSHandler& g = ::gps;
  if (g.simulating) return;  // the Bench Test has the GPS
  const uint32_t now = millis();
  const int64_t ms = (int64_t)utcMs;
  const int64_t day = ms >= 0 ? ms / 86400000 : -((-ms + 86399999) / 86400000);
  const int64_t inDay = ms - day * 86400000;
  int y, mo, d;
  civil(day, y, mo, d);

  GpsFix f = {};
  f.lat = lat;
  f.lon = lon;
  f.speedKmh = (float)speedKmh;
  f.courseValid = speedKmh > 3.0;
  f.courseDeg = (float)(headingDeg < 0 ? headingDeg + 360.0 : headingDeg);
  f.altM = 100.0f;
  f.year = (uint16_t)y;
  f.month = (uint8_t)mo;
  f.day = (uint8_t)d;
  f.gpsTimeMs = (uint32_t)inDay;
  f.rxMillis = now;
  f.hAccM = (float)accuracyM;
  f.vAccM = (float)(accuracyM * 1.5);
  f.speedAccMs = 0.15f;
  f.headingAccDeg = 0.5f;
  f.vertSpeedMs = 0;
  f.sats = (uint8_t)satellites;
  f.fixType = satellites >= 4 ? 3 : 2;
  g.sats = (uint8_t)satellites;
  g.hdopVal = (float)std::min(9.9, std::max(0.5, accuracyM * 0.6));
  f.pdop = g.hdopVal;
  g.last = f;
  g.lastSentenceMillis = now;
  g.source.onPvt(now);
  // the date and time timestampString() works from
  g.pvtTime = {};
  g.pvtTime.year = f.year;
  g.pvtTime.month = f.month;
  g.pvtTime.day = f.day;
  g.pvtTime.hour = (uint8_t)(inDay / 3600000);
  g.pvtTime.minute = (uint8_t)(inDay / 60000 % 60);
  g.pvtTime.second = (uint8_t)(inDay / 1000 % 60);
  g.pvtTimeValid = true;
  if (satellites >= GPS_MIN_SATELLITES) {
    g.queueFix(f);
    g.everFixed = true;
    g.countFixForRate(now);
  }
}

void gpsOff() {
  GPSHandler& g = ::gps;
  g.everFixed = false;
  g.lastSentenceMillis = 0;
  g.sats = 0;
  g.queueHead = g.queueCount = 0;
}

}  // namespace sim
