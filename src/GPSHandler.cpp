/**
 * Track Rat - GPS Handler Implementation
 */

#include "GPSHandler.h"
#include "Config.h"
#include "SimTrack.h"

static HardwareSerial& gpsSerial = Serial1;

void GPSHandler::begin() {
#if SIMULATE_GPS
  simulating = true;
  DEBUG_PRINTLN("GPS: SIMULATOR build - no GPS module is read");
  return;
#endif
  gpsSerial.setRxBufferSize(4096);
  if (!detect()) {
#if GPS_AUTO_SIMULATOR
    simulating = true;
    DEBUG_PRINTLN("GPS: no NMEA detected on any GPS pins - switching to SIMULATOR mode");
    return;
#else
    const int pairs[][2] = GPS_PIN_PAIRS;
    rxPin = pairs[0][0];
    txPin = pairs[0][1];
    baudRate = GPS_DEFAULT_BAUD;
    DEBUG_PRINTLN("GPS: no NMEA detected - check wiring");
#endif
  }
  gpsSerial.begin(baudRate, SERIAL_8N1, rxPin, txPin);

#if GPS_CONFIGURE_UBLOX
  configureUblox();
#endif
}

// Count bytes arriving on a pin in `ms` milliseconds. Bytes arrive (even if
// garbled) at any baud rate, so this finds which pin the GPS TX is wired to.
static size_t bytesOnPin(int rx, int tx, uint32_t baud, uint32_t ms) {
  gpsSerial.begin(baud, SERIAL_8N1, rx, tx);
  size_t n = 0;
  uint32_t start = millis();
  while (millis() - start < ms) {
    while (gpsSerial.available()) {
      gpsSerial.read();
      n++;
    }
    delay(5);
  }
  gpsSerial.end();
  return n;
}

// Find the pins (the GPS can be on the UART connector or the expansion
// header, either way round) and then the baud rate.
bool GPSHandler::detect() {
  const int pairs[][2] = GPS_PIN_PAIRS;
  for (const auto& pr : pairs) {
    size_t n = bytesOnPin(pr[0], pr[1], GPS_DEFAULT_BAUD, 1100);  // > 1 s, the slowest NMEA rate
    DEBUG_PRINTF("GPS: RX=GPIO %d: %u bytes\n", pr[0], (unsigned)n);
    if (n < 8) continue;

    const uint32_t bauds[] = {GPS_DEFAULT_BAUD, 38400, 9600, 57600, 230400};
    for (uint32_t b : bauds) {
      gpsSerial.begin(b, SERIAL_8N1, pr[0], pr[1]);
      TinyGPSPlus probe;
      uint32_t start = millis();
      while (millis() - start < 1500 && probe.passedChecksum() < 2) {
        while (gpsSerial.available()) probe.encode(gpsSerial.read());
        delay(5);
      }
      gpsSerial.end();
      if (probe.passedChecksum() >= 2) {
        rxPin = pr[0];
        txPin = pr[1];
        baudRate = b;
        DEBUG_PRINTF("GPS: detected NMEA at %lu baud (GPS TX -> GPIO %d, GPS RX -> GPIO %d)\n",
                     (unsigned long)b, rxPin, txPin);
        return true;
      }
    }
    DEBUG_PRINTF("GPS: data on GPIO %d but no valid NMEA at any baud rate\n", pr[0]);
  }
  return false;
}

// UBX-CFG-VALSET (u-blox M8 protocol 27+ / M9 / M10). Written to RAM only, so
// the module returns to factory settings on power-off. Non-u-blox modules
// ignore these bytes.
void GPSHandler::sendUbxValset(const uint8_t* cfg, size_t len) {
  uint8_t msg[64];
  size_t payloadLen = 4 + len;
  size_t n = 0;
  msg[n++] = 0xB5;
  msg[n++] = 0x62;
  msg[n++] = 0x06;  // CFG
  msg[n++] = 0x8A;  // VALSET
  msg[n++] = payloadLen & 0xFF;
  msg[n++] = payloadLen >> 8;
  msg[n++] = 0x00;  // version
  msg[n++] = 0x01;  // layer: RAM
  msg[n++] = 0x00;
  msg[n++] = 0x00;
  memcpy(&msg[n], cfg, len);
  n += len;

  uint8_t ckA = 0, ckB = 0;
  for (size_t i = 2; i < n; i++) {
    ckA += msg[i];
    ckB += ckA;
  }
  msg[n++] = ckA;
  msg[n++] = ckB;
  gpsSerial.write(msg, n);
  gpsSerial.flush();
}

void GPSHandler::configureUblox() {
  const uint16_t measMs = 1000 / GPS_TARGET_RATE_HZ;

  // Drop the NMEA sentences we don't use (GSV is large and multi-part) so
  // 10 Hz output fits comfortably in the link, and select automotive dynamics.
  const uint8_t cfg[] = {
    0x01, 0x00, 0x21, 0x30, (uint8_t)(measMs & 0xFF), (uint8_t)(measMs >> 8),  // CFG-RATE-MEAS
    0xC5, 0x00, 0x91, 0x20, 0x00,  // CFG-MSGOUT-NMEA_ID_GSV_UART1 = 0
    0xCA, 0x00, 0x91, 0x20, 0x00,  // CFG-MSGOUT-NMEA_ID_GLL_UART1 = 0
    0xC0, 0x00, 0x91, 0x20, 0x00,  // CFG-MSGOUT-NMEA_ID_GSA_UART1 = 0
    0xB1, 0x00, 0x91, 0x20, 0x00,  // CFG-MSGOUT-NMEA_ID_VTG_UART1 = 0
    0x21, 0x00, 0x11, 0x20, 0x04,  // CFG-NAVSPG-DYNMODEL = automotive
  };
  sendUbxValset(cfg, sizeof(cfg));
  DEBUG_PRINTF("GPS: requested %d Hz (u-blox VALSET, RAM only)\n", GPS_TARGET_RATE_HZ);
}

void GPSHandler::update() {
  uint32_t now = millis();

  if (simulating) {
    simulateStep();
  } else {
    readSerial(now);
  }

  if (now - rateWindowStart > 3000) {  // decay the rate if fixes stop
    rateHz = 0;
    rateWindowStart = now;
    rateWindowFixes = 0;
  }
}

void GPSHandler::readSerial(uint32_t now) {
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
#if GPS_ECHO_NMEA
    Serial.write(c);
#endif
    gps.encode(c);
  }

  if (gps.passedChecksum() != lastChecksumCount) {
    lastChecksumCount = gps.passedChecksum();
    lastSentenceMillis = now;
  }

  if (gps.satellites.isUpdated()) sats = gps.satellites.value();
  if (gps.hdop.isUpdated()) hdopVal = gps.hdop.hdop();

  // GGA and RMC both update the location each epoch; only count a fix once
  // per distinct GPS timestamp.
  if (gps.location.isUpdated() && gps.time.isValid() && gps.location.isValid()) {
    uint32_t tv = gps.time.value();
    gps.location.lat();  // clears the updated flag
    if (tv != lastTimeValue) {
      lastTimeValue = tv;
      last.lat = gps.location.lat();
      last.lon = gps.location.lng();
      last.speedKmh = gps.speed.isValid() ? (float)gps.speed.kmph() : 0.0f;
      last.gpsTimeMs = gps.time.hour() * 3600000UL + gps.time.minute() * 60000UL +
                       gps.time.second() * 1000UL + gps.time.centisecond() * 10UL;
      last.rxMillis = now;
      fixPending = true;
      everFixed = true;
      countFixForRate(now);
    }
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

bool GPSHandler::takeFix(GpsFix& out) {
  if (!fixPending) return false;
  fixPending = false;
  out = last;
  return true;
}

bool GPSHandler::hasFix() const {
  return everFixed && (millis() - last.rxMillis) < GPS_FIX_TIMEOUT_MS &&
         sats >= GPS_MIN_SATELLITES;
}

bool GPSHandler::receivingData() const {
  if (simulating) return true;
  return lastSentenceMillis != 0 && (millis() - lastSentenceMillis) < 2000;
}

String GPSHandler::timestampString() {
  if (simulating) {
    char buf[24];
    snprintf(buf, sizeof(buf), "SIM_%lu", (unsigned long)(millis() / 1000));
    return String(buf);
  }
  if (!gps.date.isValid() || !gps.time.isValid() || gps.date.year() < 2020) return String();
  char buf[24];
  snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d", gps.date.year(), gps.date.month(),
           gps.date.day(), gps.time.hour(), gps.time.minute(), gps.time.second());
  return String(buf);
}

// A car driving anticlockwise around a circle at 10 Hz. Speed varies around
// the lap and from lap to lap so best-lap and delta logic get exercised.
void GPSHandler::simulateStep() {
  uint32_t now = millis();
  if (simLastStep == 0) simLastStep = now;
  if (now - simLastStep < 100) return;
  simLastStep += 100;

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
  last.gpsTimeMs = (now + 12UL * 3600000UL) % 86400000UL;
  last.rxMillis = now;
  sats = 12;
  hdopVal = 0.8f;
  fixPending = true;
  everFixed = true;
  countFixForRate(now);
}

