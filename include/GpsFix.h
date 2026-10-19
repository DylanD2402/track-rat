/**
 * Track Rat - one GPS position fix
 *
 * Pure C++ (no Arduino), so the code that makes and uses fixes can be
 * tested on a PC.
 */

#ifndef GPS_FIX_H
#define GPS_FIX_H

#include <stdint.h>

struct GpsFix {
  double lat;
  double lon;
  float speedKmh;
  float courseDeg;      // direction of travel, 0 = north, clockwise
  bool courseValid;
  float altM;           // metres above sea level
  uint16_t year;        // UTC date, 0 if not known yet
  uint8_t month, day;
  uint32_t gpsTimeMs;   // UTC milliseconds since midnight, from the GPS
  uint32_t rxMillis;    // millis() when the fix was received
  // Quality, from UBX-NAV-PVT. With an NMEA-only GPS the accuracies are -1
  // (unknown), vertSpeedMs is 0 and pdop holds the HDOP.
  float hAccM;          // horizontal accuracy, +/- metres
  float vAccM;          // vertical accuracy, +/- metres
  float speedAccMs;     // speed accuracy, +/- m/s
  float headingAccDeg;  // direction of travel accuracy, +/- degrees
  float vertSpeedMs;    // + = climbing
  float pdop;
  uint8_t fixType;      // 2 = 2D, 3 = 3D, 4 = GNSS + dead reckoning
  uint8_t sats;         // satellites used in the fix
  bool accuracyKnown() const { return hAccM >= 0; }
};

#endif  // GPS_FIX_H
