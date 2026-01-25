/**
 * Track Rat - GPS Handler Implementation
 * u-blox NEO GPS Module Interface using TinyGPS++
 */

#include "GPSHandler.h"
#include "Config.h"
#include <math.h>

GPSHandler::GPSHandler() :
  currentLat(0.0),
  currentLon(0.0),
  currentSpeed(0.0),
  currentAltitude(0.0),
  currentTime(0),
  currentDate(0),
  satellites(0),
  validFix(false),
  lastUpdate(0),
  validFixTime(0),
  gpsSerial(nullptr)
{}

bool GPSHandler::begin(uint8_t rxPin, uint8_t txPin, uint32_t baud) {
  gpsSerial = new HardwareSerial(2);  // Use UART2 on ESP32
  gpsSerial->begin(baud, SERIAL_8N1, rxPin, txPin);

  DEBUG_PRINTLN("GPS Handler initialized");
  DEBUG_PRINTF("GPS UART: RX=%d, TX=%d, Baud=%d\n", rxPin, txPin, baud);

  return true;
}

void GPSHandler::update() {
  if (!gpsSerial) return;

  // Read available GPS data
  while (gpsSerial->available() > 0) {
    char c = gpsSerial->read();

    if (gps.encode(c)) {
      // New data available
      lastUpdate = millis();

      // Update position
      if (gps.location.isValid()) {
        currentLat = gps.location.lat();
        currentLon = gps.location.lng();
      }

      // Update speed
      if (gps.speed.isValid()) {
        currentSpeed = gps.speed.kmph();
      }

      // Update altitude
      if (gps.altitude.isValid()) {
        currentAltitude = gps.altitude.meters();
      }

      // Update time
      if (gps.time.isValid()) {
        currentTime = (uint32_t)gps.time.hour() * 3600000UL +
                      (uint32_t)gps.time.minute() * 60000UL +
                      (uint32_t)gps.time.second() * 1000UL +
                      (uint32_t)gps.time.centisecond() * 10UL;
      }

      // Update date
      if (gps.date.isValid()) {
        currentDate = (uint32_t)gps.date.year() * 10000UL +
                      (uint32_t)gps.date.month() * 100UL +
                      (uint32_t)gps.date.day();
      }

      // Update satellite count
      if (gps.satellites.isValid()) {
        satellites = gps.satellites.value();
      }

      // Check for valid fix
      bool hadFix = validFix;
      validFix = gps.location.isValid() &&
                 gps.location.age() < 2000 &&
                 satellites >= 4;

      if (validFix && !hadFix) {
        validFixTime = millis();
        DEBUG_PRINTLN("GPS fix acquired!");
      } else if (!validFix && hadFix) {
        DEBUG_PRINTLN("GPS fix lost!");
      }
    }
  }

  // Check for timeout
  if (millis() - lastUpdate > GPS_TIMEOUT && validFix) {
    validFix = false;
    DEBUG_PRINTLN("GPS data timeout");
  }
}

bool GPSHandler::hasFix() const {
  return validFix;
}

uint8_t GPSHandler::getSatellites() const {
  return satellites;
}

uint32_t GPSHandler::getFixAge() const {
  if (!validFix) return 0xFFFFFFFF;
  return millis() - validFixTime;
}

double GPSHandler::getLatitude() const {
  return currentLat;
}

double GPSHandler::getLongitude() const {
  return currentLon;
}

double GPSHandler::getSpeed() const {
  return currentSpeed;
}

double GPSHandler::getSpeedMPS() const {
  return currentSpeed / 3.6;  // km/h to m/s
}

double GPSHandler::getAltitude() const {
  return currentAltitude;
}

uint32_t GPSHandler::getTimeMillis() const {
  return currentTime;
}

uint32_t GPSHandler::getDate() const {
  return currentDate;
}

bool GPSHandler::getDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                             uint8_t& hour, uint8_t& minute, uint8_t& second) {
  if (!gps.date.isValid() || !gps.time.isValid()) {
    return false;
  }

  year = gps.date.year();
  month = gps.date.month();
  day = gps.date.day();
  hour = gps.time.hour();
  minute = gps.time.minute();
  second = gps.time.second();

  return true;
}

double GPSHandler::distanceTo(double lat, double lon) const {
  return calculateDistance(currentLat, currentLon, lat, lon);
}

double GPSHandler::calculateDistance(double lat1, double lon1, double lat2, double lon2) {
  // Haversine formula
  const double R = 6371000.0;  // Earth's radius in meters

  double lat1Rad = lat1 * DEG_TO_RAD;
  double lat2Rad = lat2 * DEG_TO_RAD;
  double deltaLat = (lat2 - lat1) * DEG_TO_RAD;
  double deltaLon = (lon2 - lon1) * DEG_TO_RAD;

  double a = sin(deltaLat / 2.0) * sin(deltaLat / 2.0) +
             cos(lat1Rad) * cos(lat2Rad) *
             sin(deltaLon / 2.0) * sin(deltaLon / 2.0);

  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

  return R * c;
}

double GPSHandler::courseTo(double lat, double lon) const {
  return TinyGPSPlus::courseTo(currentLat, currentLon, lat, lon);
}

bool GPSHandler::isValid() const {
  return validFix;
}

unsigned long GPSHandler::getLastUpdateTime() const {
  return lastUpdate;
}
