/**
 * Track Rat - binary UBX-NAV-PVT or NMEA text?
 *
 * GpsSourceSwitch watches what arrives from the GPS and decides whether
 * fixes come from u-blox NAV-PVT messages or from NMEA sentences, and when
 * the GPS needs configuring. GPSHandler tells it what arrived and acts on
 * the events it returns:
 *
 *   - NAV-PVT arrives (we asked for it, or the GPS kept it through an ESP32
 *     reset): use it, and turn NMEA off.
 *   - None within pvtWaitMs of asking: not a u-blox GPS, stay on NMEA.
 *   - NAV-PVT stops and NMEA comes back: the GPS restarted and lost its RAM
 *     settings, so ask again.
 *   - No GPS at start-up and NMEA appears later: configure it then.
 *
 * fixFromPvt() turns a NAV-PVT message into a GpsFix.
 *
 * Pure C++ (no Arduino), tested on a PC in test/test_gps_source.cpp.
 */

#ifndef GPS_SOURCE_H
#define GPS_SOURCE_H

#include <stdint.h>
#include "GpsFix.h"
#include "UbxParser.h"

enum class GpsEvent : uint8_t {
  None,
  Configure,   // a GPS started talking after start-up: send the base config
  PvtStarted,  // NAV-PVT is arriving: use it, turn NMEA off
  PvtLost,     // NAV-PVT stopped and NMEA is back (GPS restarted): send the base config again
  NoPvt,       // asked for NAV-PVT and none came: NMEA only
};

class GpsSourceSwitch {
public:
  // pvtWaitMs: how long to wait for the first NAV-PVT after asking.
  // pvtLostMs: NAV-PVT silent this long while NMEA arrives = GPS restarted.
  // Keep it above a few fix intervals (1000 ms suits 3 Hz and faster).
  explicit GpsSourceSwitch(bool useUbx = true, uint32_t pvtWaitMs = 3000, uint32_t pvtLostMs = 1000)
      : useUbx(useUbx), waitMs(pvtWaitMs), lostMs(pvtLostMs) {}

  // The base config (NAV-PVT on, NMEA GGA/RMC on) was just sent.
  // gpsTalking: a GPS was heard first (false = sent blind at start-up).
  void configSent(uint32_t now, bool gpsTalking);

  GpsEvent onNmea(uint32_t now);  // an NMEA sentence with a good checksum
  GpsEvent onPvt(uint32_t now);   // a NAV-PVT message with a good checksum
  GpsEvent poll(uint32_t now);    // call regularly: the NAV-PVT wait timeout

  bool pvtActive() const { return active; }  // fixes come from NAV-PVT, not NMEA

private:
  bool useUbx;
  uint32_t waitMs;
  uint32_t lostMs;
  bool active = false;
  bool gaveUp = false;
  bool configuredTalking = false;
  bool waiting = false;  // asked a talking GPS for NAV-PVT, timing the wait
  uint32_t askedAt = 0;
  uint32_t lastPvt = 0;
};

// Fills `out` from a NAV-PVT message received at rxMillis. `prev` is the
// previous fix: its date is kept if this message has none. Returns false
// (out untouched) if there's no usable fix, the time isn't valid, or it's
// the same epoch as `prev` (NMEA delivered it just before the switch).
bool fixFromPvt(const UbxPvt& v, uint32_t rxMillis, const GpsFix& prev, GpsFix& out);

#endif  // GPS_SOURCE_H
