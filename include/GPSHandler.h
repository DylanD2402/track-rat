/**
 * Track Rat - GPS Handler
 * u-blox NEO GPS Module Interface
 */

#ifndef GPS_HANDLER_H
#define GPS_HANDLER_H

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

class GPSHandler {
private:
  TinyGPSPlus gps;
  HardwareSerial* gpsSerial;

  // GPS data
  double currentLat;
  double currentLon;
  double currentSpeed;  // km/h
  double currentAltitude;
  uint32_t currentTime;  // GPS time in milliseconds since midnight
  uint32_t currentDate;  // GPS date
  uint8_t satellites;
  bool validFix;

  // Statistics
  unsigned long lastUpdate;
  unsigned long validFixTime;

public:
  GPSHandler();

  bool begin(uint8_t rxPin, uint8_t txPin, uint32_t baud);
  void update();

  // GPS status
  bool hasFix() const;
  uint8_t getSatellites() const;
  uint32_t getFixAge() const;

  // Position data
  double getLatitude() const;
  double getLongitude() const;
  double getSpeed() const;  // km/h
  double getSpeedMPS() const;  // m/s
  double getAltitude() const;

  // Time data
  uint32_t getTimeMillis() const;  // milliseconds since midnight
  uint32_t getDate() const;
  bool getDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                   uint8_t& hour, uint8_t& minute, uint8_t& second);

  // Distance calculations
  double distanceTo(double lat, double lon) const;
  static double calculateDistance(double lat1, double lon1, double lat2, double lon2);

  // Bearing
  double courseTo(double lat, double lon) const;

  // Data validity
  bool isValid() const;
  unsigned long getLastUpdateTime() const;
};

#endif // GPS_HANDLER_H
