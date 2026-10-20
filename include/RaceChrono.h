/**
 * Track Rat - RaceChrono's DIY Bluetooth LE API (6.3)
 *
 * The encodings of https://github.com/aollin/racechrono-ble-diy-device: GPS
 * (service 0x1FF8, characteristics 0x0003 and 0x0004) and CAN-style packets
 * (0x0001, filtered by what the app writes to 0x0002). Track Rat sends its
 * GPS at the fix rate, and each engine value (and the IMU's g) as a packet
 * with a fixed ID that a RaceChrono channel equation reads.
 *
 * Big-endian, except the CAN packet ID. Pure C++ (no Arduino), tested on a PC
 * in test/test_racechrono.cpp. Untested with RaceChrono itself.
 */

#ifndef RACECHRONO_H
#define RACECHRONO_H

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace rc {

// Track Rat's packet IDs. Each carries one big-endian signed 16-bit value:
// RaceChrono equation bytesToInt(raw, 0, 2), divided as the comment says (10.0, 1000.0).
constexpr uint32_t ID_RPM = 0x100;       // rpm
constexpr uint32_t ID_THROTTLE = 0x101;  // % x 10
constexpr uint32_t ID_COOLANT = 0x102;   // deg C x 10
constexpr uint32_t ID_INTAKE = 0x103;    // deg C x 10
constexpr uint32_t ID_LAT_G = 0x110;     // g x 1000, + = turning right (the IMU's, when fitted)
constexpr uint32_t ID_LONG_G = 0x111;    // g x 1000, + = speeding up

inline void be16(uint8_t* p, uint16_t v) { p[0] = v >> 8, p[1] = v; }
inline void be32(uint8_t* p, uint32_t v) { p[0] = v >> 24, p[1] = v >> 16, p[2] = v >> 8, p[3] = v; }

// --- GPS ---------------------------------------------------------------------------

struct Fix {
  bool valid = false;  // a position fix
  uint16_t year = 0;
  uint8_t month = 1, day = 1;
  uint32_t utcMs = 0;  // since midnight
  uint8_t sats = 0;
  double lat = 0, lon = 0;
  float altM = 0, speedKmh = 0, courseDeg = 0, hdop = -1;  // hdop < 0: not known
  bool courseValid = false;
};

// The time characteristic's 21 bits: the date and hour
inline uint32_t dateHour(uint16_t year, uint8_t month, uint8_t day, uint8_t hour) {
  return (uint32_t)(year - 2000) * 8928 + (uint32_t)(month - 1) * 744 + (uint32_t)(day - 1) * 24 + hour;
}

// The 3 sync bits: they go up whenever the date-hour changes, so the app pairs
// each GPS main value with the right time value.
class Sync {
public:
  uint8_t update(uint32_t dh) {
    if (dh != last) {
      if (any) bits = (bits + 1) & 7;
      last = dh;
      any = true;
    }
    return bits;
  }

private:
  uint32_t last = 0;
  bool any = false;
  uint8_t bits = 0;
};

inline uint16_t altitudeField(float m) {
  if (m > 2776.7f) {  // above the fine range: metres, flagged
    long v = lroundf(m + 500.0f);
    return (uint16_t)(((v < 0 ? 0 : v) & 0x7FFF) | 0x8000);
  }
  long v = lroundf((m + 500.0f) * 10.0f);
  return (uint16_t)((v < 0 ? 0 : v) & 0x7FFF);
}

inline uint16_t speedField(float kmh) {
  if (kmh > 327.67f) {
    long v = lroundf(kmh * 10.0f);
    return (uint16_t)(((v < 0 ? 0 : v) & 0x7FFF) | 0x8000);
  }
  long v = lroundf(kmh * 100.0f);
  return (uint16_t)((v < 0 ? 0 : v) & 0x7FFF);
}

// GPS main (0x0003), 20 bytes
inline void gpsMain(const Fix& f, uint8_t sync, uint8_t out[20]) {
  const uint32_t ms = f.utcMs % 3600000;  // since the hour
  const uint32_t t = (ms / 60000) * 30000 + (ms / 1000 % 60) * 500 + (ms % 1000) / 2;
  out[0] = (uint8_t)((sync & 7) << 5 | (t >> 16 & 0x1F));
  out[1] = t >> 8;
  out[2] = t;
  const uint8_t quality = f.valid ? 1 : 0;
  out[3] = (uint8_t)(quality << 6 | (f.sats > 0x3F ? 0x3F : f.sats));
  be32(out + 4, f.valid ? (uint32_t)(int32_t)lround(f.lat * 1e7) : 0x7FFFFFFF);
  be32(out + 8, f.valid ? (uint32_t)(int32_t)lround(f.lon * 1e7) : 0x7FFFFFFF);
  be16(out + 12, f.valid ? altitudeField(f.altM) : 0xFFFF);
  be16(out + 14, f.valid ? speedField(f.speedKmh) : 0xFFFF);
  float c = fmodf(f.courseDeg, 360.0f);
  if (c < 0) c += 360.0f;
  be16(out + 16, f.valid && f.courseValid ? (uint16_t)(lroundf(c * 100.0f) % 36000) : 0xFFFF);
  out[18] = f.hdop < 0 ? 0xFF : (uint8_t)(f.hdop * 10 > 254 ? 254 : lroundf(f.hdop * 10));
  out[19] = 0xFF;  // VDOP: not known
}

// GPS time (0x0004), 3 bytes
inline void gpsTime(const Fix& f, uint8_t sync, uint8_t out[3]) {
  const uint32_t dh = dateHour(f.year, f.month, f.day, (uint8_t)(f.utcMs / 3600000));
  out[0] = (uint8_t)((sync & 7) << 5 | (dh >> 16 & 0x1F));
  out[1] = dh >> 8;
  out[2] = dh;
}

// --- CAN-style packets -----------------------------------------------------------------

// CAN main (0x0001): the packet ID little-endian, then the payload. Returns the length.
inline int canPacket(uint32_t id, const uint8_t* payload, int n, uint8_t out[20]) {
  if (n < 1 || n > 16) return 0;
  out[0] = id, out[1] = id >> 8, out[2] = id >> 16, out[3] = id >> 24;
  memcpy(out + 4, payload, n);
  return 4 + n;
}

// One of Track Rat's values as a packet: the value / scale as a big-endian int16, clamped
inline int valuePacket(uint32_t id, float value, float perUnit, uint8_t out[20]) {
  long v = lroundf(value * perUnit);
  v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
  uint8_t p[2];
  be16(p, (uint16_t)(int16_t)v);
  return canPacket(id, p, 2, out);
}

// What the app asked for on the filter characteristic (0x0002): nothing until it
// writes, then deny all / allow all at an interval / allow these IDs at their own.
class Filter {
public:
  static constexpr int MAX_IDS = 16;

  // One write from the app. False for a command it doesn't understand (ignored).
  bool apply(const uint8_t* d, int n) {
    if (n < 1) return false;
    switch (d[0]) {
      case 0:  // deny all (before allowing some)
        if (n != 1) return false;
        heard = true, all = false, count = 0;
        return true;
      case 1:  // allow all, at an interval
        if (n != 3) return false;
        heard = true, all = true, count = 0;
        allInterval = (uint16_t)(d[1] << 8 | d[2]);
        return true;
      case 2: {  // allow one ID, at its interval
        if (n != 7) return false;
        heard = true;
        const uint16_t iv = (uint16_t)(d[1] << 8 | d[2]);
        const uint32_t id = (uint32_t)d[3] << 24 | (uint32_t)d[4] << 16 | (uint32_t)d[5] << 8 | d[6];
        for (int i = 0; i < count; i++)
          if (ids[i].id == id) return ids[i].interval = iv, true;
        if (count >= MAX_IDS) return false;
        ids[count++] = {id, iv, 0, false};
        return true;
      }
      default: return false;
    }
  }
  // Should packet `id` go now? Notes the time if so.
  bool due(uint32_t id, uint32_t nowMs) {
    if (!heard) return false;  // RaceChrono hasn't said what it wants yet
    for (int i = 0; i < count; i++)
      if (ids[i].id == id) return ready(ids[i].lastMs, ids[i].sentAny, ids[i].interval, nowMs);
    if (!all) return false;
    for (int i = 0; i < ALL_SLOTS; i++)
      if (allSlots[i].id == id || !allSlots[i].used) {
        allSlots[i].id = id, allSlots[i].used = true;
        return ready(allSlots[i].lastMs, allSlots[i].sentAny, allInterval, nowMs);
      }
    return false;
  }
  bool heardFromApp() const { return heard; }
  void reset() { *this = Filter(); }  // the app disconnected

private:
  struct Item {
    uint32_t id;
    uint16_t interval;
    uint32_t lastMs;
    bool sentAny;
  };
  struct Slot {
    uint32_t id = 0, lastMs = 0;
    bool used = false, sentAny = false;
  };
  static constexpr int ALL_SLOTS = 8;  // Track Rat sends six IDs
  bool heard = false, all = false;
  uint16_t allInterval = 0;
  Item ids[MAX_IDS];
  int count = 0;
  Slot allSlots[ALL_SLOTS];

  static bool ready(uint32_t& last, bool& any, uint16_t interval, uint32_t now) {
    if (any && now - last < interval) return false;
    last = now;
    any = true;
    return true;
  }
};

}  // namespace rc

#endif  // RACECHRONO_H
