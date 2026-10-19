/**
 * Track Rat - binary UBX-NAV-PVT or NMEA text? Implementation
 */

#include "GpsSource.h"

// Milliseconds from `from` to `to`, correct across millis() wrapping round
// and negative if `to` is a little earlier (a `now` taken before `from`).
static int32_t elapsed(uint32_t from, uint32_t to) { return (int32_t)(to - from); }

void GpsSourceSwitch::configSent(uint32_t now, bool gpsTalking) {
  active = false;  // the base config turns NMEA back on until NAV-PVT is seen
  gaveUp = false;
  // Sent blind with no GPS heard: nothing to wait for. A GPS that turns up
  // later gets a Configure event, and the wait starts from that config.
  waiting = gpsTalking;
  askedAt = now;
  if (gpsTalking) configuredTalking = true;
}

GpsEvent GpsSourceSwitch::onNmea(uint32_t now) {
  if (!configuredTalking) {
    configuredTalking = true;
    return GpsEvent::Configure;
  }
  if (active && elapsed(lastPvt, now) > (int32_t)lostMs) {
    active = false;
    return GpsEvent::PvtLost;
  }
  return GpsEvent::None;
}

GpsEvent GpsSourceSwitch::onPvt(uint32_t now) {
  if (!useUbx) return GpsEvent::None;
  lastPvt = now;
  configuredTalking = true;  // only our config turns NAV-PVT on
  if (active) return GpsEvent::None;
  active = true;
  gaveUp = false;
  return GpsEvent::PvtStarted;
}

GpsEvent GpsSourceSwitch::poll(uint32_t now) {
  if (!useUbx || active || gaveUp || !waiting || elapsed(askedAt, now) <= (int32_t)waitMs) return GpsEvent::None;
  gaveUp = true;
  return GpsEvent::NoPvt;
}

bool fixFromPvt(const UbxPvt& v, uint32_t rxMillis, const GpsFix& prev, GpsFix& out) {
  if (!v.fixOk() || !v.timeValid()) return false;
  uint32_t ms = v.utcMsOfDay();
  if (prev.rxMillis && ms == prev.gpsTimeMs) return false;

  GpsFix f = prev;
  f.lat = v.latDeg();
  f.lon = v.lonDeg();
  f.speedKmh = v.speedMs() * 3.6f;
  f.courseValid = f.speedKmh > 3.0f;  // heading is noise when slow
  f.courseDeg = v.headingDeg();
  f.altM = v.altMslM();
  if (v.dateValid() && v.year >= 2020) {
    f.year = v.year;
    f.month = v.month;
    f.day = v.day;
  }
  f.gpsTimeMs = ms;
  f.rxMillis = rxMillis;
  f.hAccM = v.hAccM();
  f.vAccM = v.vAccM();
  f.speedAccMs = v.speedAccMs();
  f.headingAccDeg = v.headingAccDeg();
  f.vertSpeedMs = v.vertSpeedMs();
  f.pdop = v.pdop();
  f.fixType = v.fixType;
  f.sats = v.numSV;
  out = f;
  return true;
}
