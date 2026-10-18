/**
 * Track Rat - GPS Handler
 *
 * Reads NMEA from the Beitian BE-252Q over UART1 using TinyGPS++.
 * - Auto-detects the module's baud rate at startup.
 * - Optionally asks a u-blox based module for 10 Hz updates (RAM only).
 * - Delivers each new position fix exactly once via takeFix().
 *
 * Built with SIMULATE_GPS=1 it instead generates a car lapping a small
 * circuit, so the whole UI and lap logic can be tested at a desk.
 */

#ifndef GPS_HANDLER_H
#define GPS_HANDLER_H

#include <Arduino.h>
#include <TinyGPSPlus.h>

struct GpsFix {
  double lat;
  double lon;
  float speedKmh;
  uint32_t gpsTimeMs;   // UTC milliseconds since midnight, from the GPS
  uint32_t rxMillis;    // millis() when the fix was received
};

class GPSHandler {
public:
  void begin();
  void update();

  // Returns true once per new position fix.
  bool takeFix(GpsFix& out);

  bool hasFix() const;
  bool receivingData() const;     // valid NMEA seen in the last 2 s
  uint8_t satellites() const { return sats; }
  float hdop() const { return hdopVal; }
  float fixRateHz() const { return rateHz; }
  uint32_t baud() const { return baudRate; }
  const GpsFix& lastFix() const { return last; }

  // "YYYYMMDD_HHMMSS" from GPS date/time, or empty if unknown.
  String timestampString();

private:
  TinyGPSPlus gps;
  uint32_t baudRate = 0;
  GpsFix last = {};
  bool fixPending = false;
  bool everFixed = false;
  uint32_t lastTimeValue = 0xFFFFFFFF;
  uint32_t lastSentenceMillis = 0;
  uint32_t lastChecksumCount = 0;
  uint8_t sats = 0;
  float hdopVal = 99.9f;

  // fix-rate measurement
  uint32_t rateWindowStart = 0;
  uint16_t rateWindowFixes = 0;
  float rateHz = 0;

  uint32_t detectBaud();
  void configureUblox();
  void sendUbxValset(const uint8_t* cfg, size_t len);
  void countFixForRate(uint32_t now);

#if SIMULATE_GPS
  uint32_t simLastStep = 0;
  double simAngle = 0;
  void simulateStep();
#endif
};

#endif  // GPS_HANDLER_H
