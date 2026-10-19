/**
 * Track Rat - GPS Handler Implementation
 */

#include "GPSHandler.h"
#include "Config.h"
#include "SimTrack.h"

static HardwareSerial& gpsSerial = Serial1;

void GPSHandler::begin() {
  gpsSerial.setRxBufferSize(GPS_RX_BUFFER_BYTES);  // rides out SD writes etc. without losing data
  if (!detect()) {
    // Listen on the UART connector anyway, so a GPS plugged in later works.
    const int pairs[][2] = GPS_PIN_PAIRS;
    rxPin = pairs[0][0];
    txPin = pairs[0][1];
    baudRate = GPS_DEFAULT_BAUD;
    DEBUG_PRINTF("GPS: no NMEA detected - listening on GPIO %d at %lu baud\n", rxPin, (unsigned long)baudRate);
  }
  gpsSerial.begin(baudRate, SERIAL_8N1, rxPin, txPin);

#if GPS_CONFIGURE_UBLOX
  configureUblox();
#endif
}

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

// --- u-blox configuration (UBX protocol; non-u-blox modules ignore it) ---

void GPSHandler::sendUbx(uint8_t cls, uint8_t id, const uint8_t* payload, size_t len) {
  uint8_t head[6] = {0xB5, 0x62, cls, id, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
  uint8_t ckA = 0, ckB = 0;
  for (int i = 2; i < 6; i++) { ckA += head[i]; ckB += ckA; }
  for (size_t i = 0; i < len; i++) { ckA += payload[i]; ckB += ckA; }
  uint8_t tail[2] = {ckA, ckB};
  gpsSerial.write(head, 6);
  gpsSerial.write(payload, len);
  gpsSerial.write(tail, 2);
  gpsSerial.flush();
}

// UBX-CFG-VALSET. layers: 0x01 RAM, 0x02 battery-backed RAM (survives a
// warm restart while the module keeps backup power).
void GPSHandler::sendUbxValset(uint8_t layers, const uint8_t* cfg, size_t len) {
  uint8_t payload[80];
  payload[0] = 0x00;  // version
  payload[1] = layers;
  payload[2] = payload[3] = 0x00;
  memcpy(payload + 4, cfg, len);
  sendUbx(0x06, 0x8A, payload, 4 + len);
}

// Minimal UBX frame reader, fed alongside TinyGPS++. Only the
// UBX-CFG-VALGET response is used (to read the satellite-system settings).
void GPSHandler::feedUbx(uint8_t c) {
  switch (ubxState) {
    case 0: ubxState = (c == 0xB5) ? 1 : 0; return;
    case 1: ubxState = (c == 0x62) ? 2 : 0; return;
    case 2: ubxCls = c; ubxState = 3; return;
    case 3: ubxId = c; ubxState = 4; return;
    case 4: ubxLen = c; ubxState = 5; return;
    case 5:
      ubxLen |= c << 8;
      ubxPos = 0;
      ubxState = ubxLen <= sizeof(ubxBuf) ? (ubxLen ? 6 : 7) : 0;
      return;
    case 6:
      ubxBuf[ubxPos++] = c;
      if (ubxPos >= ubxLen) ubxState = 7;
      return;
    case 7: ubxCkA = c; ubxState = 8; return;
    case 8: {
      ubxState = 0;
      uint8_t a = 0, b = 0;
      const uint8_t head[4] = {ubxCls, ubxId, (uint8_t)(ubxLen & 0xFF), (uint8_t)(ubxLen >> 8)};
      for (uint8_t h : head) { a += h; b += a; }
      for (uint16_t i = 0; i < ubxLen; i++) { a += ubxBuf[i]; b += a; }
      if (a != ubxCkA || b != c) return;
      if (ubxCls == 0x06 && ubxId == 0x8B && ubxLen >= 4) {  // CFG-VALGET response
        signalsRead = 0;
        for (uint16_t i = 4; i + 5 <= ubxLen; i += 5) {   // 1-byte (L) values only
          uint32_t key = ubxBuf[i] | ubxBuf[i + 1] << 8 | ubxBuf[i + 2] << 16 | (uint32_t)ubxBuf[i + 3] << 24;
          for (int k = 0; k < SIGNAL_KEYS; k++)
            if (key == signalKey(k)) {
              signalValue[k] = ubxBuf[i + 4];
              signalsRead |= 1 << k;
            }
        }
      }
      return;
    }
  }
}

// GPS, Galileo, GLONASS, BeiDou, QZSS enable keys (CFG-SIGNAL-*_ENA)
uint32_t GPSHandler::signalKey(int k) {
  static const uint32_t keys[SIGNAL_KEYS] = {0x1031001F, 0x10310021, 0x10310025, 0x10310022, 0x10310024};
  return keys[k];
}

void GPSHandler::configureUblox() {
  const uint16_t measMs = 1000 / GPS_TARGET_RATE_HZ;

  // Rate, automotive dynamics, and only the NMEA sentences we use (GGA, RMC)
  // so high rates fit easily in the link. RAM only: no restart needed.
  const uint8_t cfg[] = {
    0x01, 0x00, 0x21, 0x30, (uint8_t)(measMs & 0xFF), (uint8_t)(measMs >> 8),  // CFG-RATE-MEAS
    0xC5, 0x00, 0x91, 0x20, 0x00,  // CFG-MSGOUT-NMEA_ID_GSV_UART1 = 0
    0xCA, 0x00, 0x91, 0x20, 0x00,  // CFG-MSGOUT-NMEA_ID_GLL_UART1 = 0
    0xC0, 0x00, 0x91, 0x20, 0x00,  // CFG-MSGOUT-NMEA_ID_GSA_UART1 = 0
    0xB1, 0x00, 0x91, 0x20, 0x00,  // CFG-MSGOUT-NMEA_ID_VTG_UART1 = 0
    0x21, 0x00, 0x11, 0x20, 0x04,  // CFG-NAVSPG-DYNMODEL = automotive
  };
  sendUbxValset(0x01, cfg, sizeof(cfg));
  DEBUG_PRINTF("GPS: requested %d Hz (u-blox CFG-RATE)\n", GPS_TARGET_RATE_HZ);

#if GPS_SET_CONSTELLATIONS
  // Changing satellite systems restarts tracking (the fix drops for a few
  // seconds), so read the current setting first and only change it if needed.
  const uint8_t want[SIGNAL_KEYS] = {1, GPS_USE_GALILEO, GPS_USE_GLONASS, GPS_USE_BEIDOU, 1 /* QZSS goes with GPS */};
  uint8_t get[4 + 4 * SIGNAL_KEYS] = {0x00, 0x00, 0x00, 0x00};  // version, layer RAM, position
  for (int k = 0; k < SIGNAL_KEYS; k++) {
    uint32_t key = signalKey(k);
    for (int i = 0; i < 4; i++) get[4 + 4 * k + i] = (key >> (8 * i)) & 0xFF;
  }
  signalsRead = 0;
  sendUbx(0x06, 0x8B, get, sizeof(get));
  uint32_t start = millis();
  while (millis() - start < 700 && signalsRead != (1 << SIGNAL_KEYS) - 1) {
    readSerial(millis());
    delay(5);
  }

  static const char* names[SIGNAL_KEYS] = {"GPS", "Galileo", "GLONASS", "BeiDou", "QZSS"};
  if (signalsRead == (1 << SIGNAL_KEYS) - 1) {
    String on;
    bool same = true;
    for (int k = 0; k < SIGNAL_KEYS; k++) {
      if (signalValue[k]) on += String(on.length() ? "+" : "") + names[k];
      same &= (signalValue[k] != 0) == (want[k] != 0);
    }
    DEBUG_PRINTF("GPS: satellite systems in use: %s\n", on.c_str());
    if (same) return;
  } else {
    DEBUG_PRINTLN("GPS: could not read satellite systems (not a u-blox M10?) - leaving them as they are");
    return;
  }

  uint8_t set[5 * SIGNAL_KEYS];
  String on;
  for (int k = 0; k < SIGNAL_KEYS; k++) {
    uint32_t key = signalKey(k);
    for (int i = 0; i < 4; i++) set[5 * k + i] = (key >> (8 * i)) & 0xFF;
    set[5 * k + 4] = want[k];
    if (want[k]) on += String(on.length() ? "+" : "") + names[k];
  }
  sendUbxValset(0x03, set, sizeof(set));  // RAM + battery-backed, so next boot needn't change it
  DEBUG_PRINTF("GPS: switching satellite systems to %s - tracking restarts, fix back in a few seconds\n",
               on.c_str());
#endif
}

void GPSHandler::update() {
  uint32_t now = millis();

  if (simulating) {
    while (gpsSerial.available()) gpsSerial.read();  // keep the real GPS's buffer drained
    if (simMode == SimMode::Launch) simulateLaunchStep();
    else simulateStep();
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
    feedUbx((uint8_t)c);
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
      last.courseValid = gps.course.isValid() && last.speedKmh > 3.0f;  // heading is noise when slow
      last.courseDeg = gps.course.isValid() ? (float)gps.course.deg() : 0.0f;
      last.altM = gps.altitude.isValid() ? (float)gps.altitude.meters() : 0.0f;
      if (gps.date.isValid() && gps.date.year() >= 2020) {
        last.year = gps.date.year();
        last.month = gps.date.month();
        last.day = gps.date.day();
      }
      last.gpsTimeMs = gps.time.hour() * 3600000UL + gps.time.minute() * 60000UL +
                       gps.time.second() * 1000UL + gps.time.centisecond() * 10UL;
      last.rxMillis = now;
      queueFix(last);
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

// Fixes are queued so none are lost if the main loop is briefly busy
// (drawing, writing to the SD card): every one reaches the lap timer.
void GPSHandler::queueFix(const GpsFix& f) {
  if (queueCount == FIX_QUEUE_LEN) {  // full: drop the oldest
    queueHead = (queueHead + 1) % FIX_QUEUE_LEN;
    queueCount--;
    droppedFixes++;
  }
  fixQueue[(queueHead + queueCount) % FIX_QUEUE_LEN] = f;
  queueCount++;
}

bool GPSHandler::takeFix(GpsFix& out) {
  if (queueCount == 0) return false;
  out = fixQueue[queueHead];
  queueHead = (queueHead + 1) % FIX_QUEUE_LEN;
  queueCount--;
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
  // Moving anticlockwise: direction of travel is (-sin a, cos a) east/north
  float course = atan2f(-sinf((float)a), cosf((float)a)) * RAD_TO_DEG;
  last.courseDeg = course < 0 ? course + 360.0f : course;
  last.courseValid = true;
  last.altM = 100.0f;
  last.year = 2026;
  last.month = 1;
  last.day = 1;
  last.gpsTimeMs = (now + 12UL * 3600000UL) % 86400000UL;
  last.rxMillis = now;
  sats = 12;
  hdopVal = 0.8f;
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
  last.altM = 100.0f;
  last.year = 2026;
  last.month = 1;
  last.day = 1;
  last.gpsTimeMs = (simClockMs + 12UL * 3600000UL) % 86400000UL;
  last.rxMillis = now;
  sats = 12;
  hdopVal = 0.8f;
  queueFix(last);
  everFixed = true;
  countFixForRate(now);
}
