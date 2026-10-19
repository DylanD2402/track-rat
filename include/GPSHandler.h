/**
 * Track Rat - GPS Handler
 *
 * Reads the Beitian BE-252Q over UART1.
 * - Finds which pins the module is on and its baud rate at startup.
 * - Asks a u-blox based module for 20 Hz updates and binary UBX-NAV-PVT
 *   messages (RAM only). Once those arrive the NMEA sentences are turned
 *   off. A GPS that never sends them (not u-blox) is read as NMEA through
 *   TinyGPS++.
 * - Delivers each new position fix exactly once via takeFix().
 *
 * In bench-test mode (setSimulated) it instead generates a car lapping a
 * small circuit, so the UI and lap logic can be tested at a desk.
 */

#ifndef GPS_HANDLER_H
#define GPS_HANDLER_H

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include "GpsFix.h"
#include "GpsSource.h"
#include "UbxParser.h"

// What the simulated GPS does: lap a circle (lap timing) or repeat launch, top speed, brake (Performance).
enum class SimMode { Circuit, Launch };

class GPSHandler {
public:
  void begin();
  void update();

  // Returns each new position fix once, oldest first (call until false).
  bool takeFix(GpsFix& out);

  bool hasFix() const;
  bool receivingData() const;     // valid NMEA or UBX seen in the last 2 s
  bool usingUbx() const { return source.pvtActive(); }  // fixes come from UBX-NAV-PVT
  uint8_t satellites() const { return sats; }
  float hdop() const { return hdopVal; }  // PDOP when using UBX
  // Horizontal accuracy of the latest fix, +/- metres; -1 if not known
  // (NMEA-only GPS, or no fix yet).
  float accuracyM() const { return last.rxMillis ? last.hAccM : -1.0f; }
  float fixRateHz() const { return rateHz; }
  uint32_t baud() const { return baudRate; }
  int rxPinUsed() const { return rxPin; }
  int txPinUsed() const { return txPin; }
  bool isSimulated() const { return simulating; }
  uint32_t droppedFixCount() const { return droppedFixes; }
  void setSimulated(bool on, SimMode mode = SimMode::Circuit);
  const GpsFix& lastFix() const { return last; }

  // "YYYYMMDD_HHMMSS" from GPS date/time, or empty if unknown.
  String timestampString();

private:
  TinyGPSPlus gps;
  uint32_t baudRate = 0;
  GpsFix last = {};
  static const int FIX_QUEUE_LEN = 64;  // 3 s at 20 Hz
  GpsFix fixQueue[FIX_QUEUE_LEN];
  int queueHead = 0, queueCount = 0;
  uint32_t droppedFixes = 0;
  void queueFix(const GpsFix& f);
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

  bool detect();
  int rxPin = -1, txPin = -1;
  void configureUblox();
  void sendUbx(uint8_t cls, uint8_t id, const uint8_t* payload, size_t len);
  void sendUbxValset(uint8_t layers, const uint8_t* cfg, size_t len);

  bool detected = false;          // a GPS was found at start-up
  void sendBaseConfig(bool gpsTalking);
  void setNmeaOutput(bool on);

  // UBX messages from the GPS: NAV-PVT fixes and the CFG-VALGET reply
  UbxParser ubx;
  GpsSourceSwitch source;         // NAV-PVT or NMEA (see GpsSource.h)
  void handleUbxFrame(uint32_t now);
  void onPvt(const UbxPvt& v, uint32_t now);
  void onSourceEvent(GpsEvent e);
  bool pvtTimeValid = false;      // latest NAV-PVT date and time, for timestampString()
  UbxPvt pvtTime = {};
  static const int SIGNAL_KEYS = 5;
  static uint32_t signalKey(int k);
  uint8_t signalValue[SIGNAL_KEYS] = {};
  uint8_t signalsRead = 0;
  void countFixForRate(uint32_t now);
  void readSerial(uint32_t now);
  void takeNmeaFix(uint32_t now);

  bool simulating = false;
  uint32_t simLastStep = 0;
  double simAngle = 0;
  void simulateStep();
  SimMode simMode = SimMode::Circuit;
  uint8_t simPhase = 0;          // launch demo: 0 stopped, 1 accelerating, 2 braking
  double simPhaseT = 0, simV = 0, simDist = 0;
  uint32_t simClockMs = 0;
  void simulateLaunchStep();
};

#endif  // GPS_HANDLER_H
