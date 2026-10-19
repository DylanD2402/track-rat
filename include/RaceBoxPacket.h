/**
 * Track Rat - RaceBox Mini data packet
 *
 * Builds the 88-byte "RaceBox Data Message" (UBX-style framing, class 0xFF,
 * id 0x01, 80-byte payload) that RaceBox Mini devices send over BLE once per
 * GNSS fix. Apps that support the RaceBox Mini (RaceChrono, Solostorm, ...)
 * read it as a GPS source.
 *
 * Pure C++ (no Arduino), tested on a PC in test/test_racebox.cpp.
 */

#ifndef RACEBOX_PACKET_H
#define RACEBOX_PACKET_H

#include <stdint.h>
#include <stddef.h>

constexpr size_t RACEBOX_PACKET_LEN = 88;
constexpr size_t RACEBOX_PAYLOAD_LEN = 80;

struct RaceBoxFix {
  bool dateValid;
  bool timeValid;
  uint16_t year;
  uint8_t month, day, hour, minute, second;
  uint16_t millis;
  bool hasFix;            // position valid
  uint8_t satellites;
  double lat, lon;        // degrees
  float altMslM;          // metres above sea level
  float speedMs;
  float headingDeg;       // direction of travel
  bool headingValid;
  float hdop;             // PDOP when accuracyKnown
  // The GPS's own accuracy figures (u-blox UBX-NAV-PVT). Without them
  // (NMEA-only GPS) accuracy is estimated from hdop.
  bool accuracyKnown;
  uint8_t fixType;        // 2 = 2D, 3 = 3D, 4 = GNSS + dead reckoning
  float hAccM, vAccM;     // +/- metres
  float speedAccMs;       // +/- m/s
  float headingAccDeg;    // +/- degrees
  // Accelerometer (milli-g) and gyro (centi-degrees/s). No IMU on this
  // board: 0, 0, +1000 is a device lying still.
  int16_t gX, gY, gZ;
  int16_t rX, rY, rZ;
  uint8_t batteryPercent;
};

// GPS time of week in milliseconds (GPS time = UTC + 18 s leap seconds).
uint32_t gpsTimeOfWeekMs(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
                         uint8_t second, uint16_t millis);

// Fills out[RACEBOX_PACKET_LEN].
void buildRaceBoxPacket(const RaceBoxFix& f, uint8_t* out);

#endif  // RACEBOX_PACKET_H
