/**
 * Host-side tests for choosing between UBX-NAV-PVT and NMEA (GpsSource),
 * and an end-to-end simulation: a simulated GPS sends bytes, which go
 * through UbxParser, GpsSourceSwitch and fixFromPvt into the LapTimer.
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_gps_source.cpp src/GpsSource.cpp src/UbxParser.cpp src/LapTimer.cpp -o /tmp/gs && /tmp/gs
 *
 * The Receiver below mirrors GPSHandler::readSerial / onPvt / onSourceEvent,
 * which can't run here (Arduino serial port, TinyGPS++). NMEA sentences are
 * only counted (checksum-checked), not decoded: on the board TinyGPS++ does that.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "GpsSource.h"
#include "LapTimer.h"
#include "UbxParser.h"
#include "check.h"

// ---------------------------------------------------------------------------
// Building GPS output

static void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF; }

// A NAV-PVT frame (100 bytes) from the fields that matter here.
static std::vector<uint8_t> encodePvt(const UbxPvt& v) {
  std::vector<uint8_t> f(100, 0);
  f[0] = 0xB5; f[1] = 0x62; f[2] = 0x01; f[3] = 0x07;
  put16(&f[4], 92);
  uint8_t* p = &f[6];
  put32(p + 0, v.iTOW);
  put16(p + 4, v.year);
  p[6] = v.month; p[7] = v.day; p[8] = v.hour; p[9] = v.minute; p[10] = v.second; p[11] = v.valid;
  put32(p + 12, v.tAcc);
  put32(p + 16, (uint32_t)v.nano);
  p[20] = v.fixType; p[21] = v.flags; p[22] = v.flags2; p[23] = v.numSV;
  put32(p + 24, (uint32_t)v.lon);
  put32(p + 28, (uint32_t)v.lat);
  put32(p + 32, (uint32_t)v.height);
  put32(p + 36, (uint32_t)v.hMSL);
  put32(p + 40, v.hAcc);
  put32(p + 44, v.vAcc);
  put32(p + 48, (uint32_t)v.velN);
  put32(p + 52, (uint32_t)v.velE);
  put32(p + 56, (uint32_t)v.velD);
  put32(p + 60, (uint32_t)v.gSpeed);
  put32(p + 64, (uint32_t)v.headMot);
  put32(p + 68, v.sAcc);
  put32(p + 72, v.headAcc);
  put16(p + 76, v.pDOP);
  p[78] = v.flags3;
  uint8_t a = 0, b = 0;
  for (int i = 2; i < 98; i++) { a += f[i]; b += a; }
  f[98] = a; f[99] = b;
  return f;
}

static std::string nmea(const std::string& body) {
  uint8_t cs = 0;
  for (char c : body) cs ^= (uint8_t)c;
  char tail[8];
  snprintf(tail, sizeof(tail), "*%02X\r\n", cs);
  return "$" + body + tail;
}

// Counts NMEA sentences with a good checksum, like TinyGPS++'s passedChecksum().
struct NmeaCounter {
  std::string line;
  bool in = false;
  bool feed(uint8_t c) {
    if (c == '$') { in = true; line.clear(); return false; }
    if (!in) return false;
    if (c == '\r' || c == '\n') {
      in = false;
      size_t star = line.find('*');
      if (star == std::string::npos || star + 3 > line.size()) return false;
      uint8_t cs = 0;
      for (size_t i = 0; i < star; i++) cs ^= (uint8_t)line[i];
      return strtoul(line.substr(star + 1, 2).c_str(), nullptr, 16) == cs;
    }
    if (c < 0x20 || c > 0x7E || line.size() > 90) { in = false; return false; }  // binary: not NMEA
    line += (char)c;
    return false;
  }
};

// ---------------------------------------------------------------------------
// Simulated GPS: a car going round a circle, reacting to config like an M10

enum class Cmd { BaseConfig, NmeaOff };

static const double SF_LAT = 45.0, SF_LON = -76.0, RADIUS = 150.0, SPEED = 30.0;

struct SimGps {
  bool ublox = true;
  bool useUbx = true;  // what the base config asks for (GPS_USE_UBX)
  bool nmeaOn = true, pvtOn = false;
  bool silent = false;  // unplugged / restarting
  uint32_t startUtcMs = 12 * 3600000u;  // UTC time of epoch 0
  double angle = -0.2;
  std::vector<Cmd> pending;

  void command(Cmd c) { pending.push_back(c); }
  void restart() { nmeaOn = true; pvtOn = false; }  // factory output, RAM settings gone

  // Bytes for epoch i (50 ms apart). Commands sent before this epoch apply first.
  std::vector<uint8_t> epoch(int i, bool fix = true) {
    if (ublox) {
      for (Cmd c : pending) {
        if (c == Cmd::BaseConfig) { nmeaOn = true; pvtOn = useUbx; }
        if (c == Cmd::NmeaOff) nmeaOn = false;
      }
    }
    pending.clear();

    std::vector<uint8_t> out;
    uint32_t ms = (uint32_t)(((uint64_t)startUtcMs + 50u * (uint64_t)i) % 86400000u);
    double lat = SF_LAT + RADIUS * sin(angle) / 111194.93;
    double lon = SF_LON + RADIUS * (cos(angle) - 1.0) / (111194.93 * cos(SF_LAT * geo::DEG2RAD));
    angle += SPEED * 0.05 / RADIUS;
    if (silent) return out;

    if (nmeaOn) {
      char body[96];
      snprintf(body, sizeof(body), "GNGGA,%02u%02u%02u.%02u,4500.00000,N,07600.00000,W,1,10,0.9,100.0,M,-34.0,M,,",
               ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000 / 10);
      std::string s = nmea(body);
      snprintf(body, sizeof(body), "GNRMC,%02u%02u%02u.%02u,A,4500.00000,N,07600.00000,W,58.3,90.0,141026,,,A",
               ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000 / 10);
      s += nmea(body);
      out.insert(out.end(), s.begin(), s.end());
    }
    if (pvtOn) {
      UbxPvt v = {};
      v.iTOW = 100000000u + 50u * i;
      v.year = 2026; v.month = 10; v.day = 14;
      v.hour = ms / 3600000; v.minute = ms / 60000 % 60; v.second = ms / 1000 % 60;
      v.nano = (int32_t)(ms % 1000) * 1000000 - 7;  // a few ns early, as receivers report
      v.valid = 0x07;
      v.tAcc = 20;
      v.fixType = fix ? 3 : 0;
      v.flags = fix ? 0x01 : 0;
      v.numSV = fix ? 12 : 2;
      v.lat = (int32_t)lround(lat * 1e7);
      v.lon = (int32_t)lround(lon * 1e7);
      v.hMSL = 100000;
      v.hAcc = fix ? 850 : 0xFFFFFFFF;
      v.vAcc = 1400;
      v.gSpeed = (int32_t)(SPEED * 1000);
      v.velD = -120;
      v.headMot = 9000000;
      v.sAcc = 140;
      v.headAcc = 60000;
      v.pDOP = 120;
      std::vector<uint8_t> f = encodePvt(v);
      out.insert(out.end(), f.begin(), f.end());
    }
    return out;
  }
};

// ---------------------------------------------------------------------------
// Receiver: what GPSHandler does with the bytes

struct Receiver {
  UbxParser ubx;
  NmeaCounter nmeaCount;
  GpsSourceSwitch sw;
  bool configure = true;  // GPS_CONFIGURE_UBLOX
  SimGps* gps = nullptr;
  GpsFix last = {};
  std::vector<GpsFix> fixes;
  int configs = 0, nmeaOffs = 0;
  int ev[5] = {};  // count of each GpsEvent

  explicit Receiver(bool useUbx = true) : sw(useUbx, 3000) {}

  void begin(uint32_t now, bool detected) {
    if (configure) sendBase(now, detected);
  }
  void sendBase(uint32_t now, bool talking) {
    configs++;
    gps->command(Cmd::BaseConfig);
    sw.configSent(now, talking);
  }
  void act(GpsEvent e, uint32_t now) {
    ev[(int)e]++;
    switch (e) {
      case GpsEvent::Configure: if (configure) sendBase(now, true); break;
      case GpsEvent::PvtStarted: if (configure) { nmeaOffs++; gps->command(Cmd::NmeaOff); } break;
      case GpsEvent::PvtLost: if (configure) sendBase(now, true); break;
      default: break;
    }
  }
  void read(const uint8_t* b, size_t n, uint32_t now) {
    for (size_t i = 0; i < n; i++) {
      if (nmeaCount.feed(b[i])) act(sw.onNmea(now), now);  // per sentence, as GPSHandler does
      if (!ubx.feed(b[i])) continue;
      UbxPvt v;
      if (!ubx.is(UBX_CLASS_NAV, UBX_ID_NAV_PVT) || !decodeNavPvt(ubx.payload(), ubx.length(), v)) continue;
      act(sw.onPvt(now), now);
      if (sw.pvtActive() && fixFromPvt(v, now, last, last)) fixes.push_back(last);
    }
    act(sw.poll(now), now);
  }
  int count(GpsEvent e) const { return ev[(int)e]; }
};

// Delivers bytes the way UART reads do: random chunks, sometimes held back
// for a while (the main loop busy drawing or writing to the SD card).
struct Link {
  std::vector<uint8_t> buf;
  uint32_t heldUntil = 0;
  void send(const std::vector<uint8_t>& b) { buf.insert(buf.end(), b.begin(), b.end()); }
  void deliver(Receiver& r, uint32_t now, bool stalls) {
    if (stalls && heldUntil == 0 && testRand() % 200 == 0) heldUntil = now + 300 + testRand() % 900;
    if (now < heldUntil) return;
    heldUntil = 0;
    size_t off = 0;
    while (off < buf.size()) {
      size_t n = 1 + testRand() % 160;
      if (off + n > buf.size()) n = buf.size() - off;
      r.read(&buf[off], n, now);
      off += n;
    }
    buf.clear();
  }
};

// ---------------------------------------------------------------------------
// GpsSourceSwitch on its own

static void testSwitchUnits() {
  {  // u-blox: config, NMEA while waiting, then NAV-PVT
    GpsSourceSwitch s(true, 3000);
    s.configSent(1000, true);
    CHECK(s.onNmea(1100) == GpsEvent::None, "nmea while waiting");
    CHECK(!s.pvtActive(), "not active yet");
    CHECK(s.onPvt(1150) == GpsEvent::PvtStarted && s.pvtActive(), "pvt started");
    CHECK(s.onPvt(1200) == GpsEvent::None, "second pvt");
    CHECK(s.onNmea(1210) == GpsEvent::None, "NMEA still trickling in just after the switch");
    CHECK(s.poll(9000) == GpsEvent::None, "no timeout once active");
  }
  {  // not u-blox: NMEA only, gives up once, just after the wait
    GpsSourceSwitch s(true, 3000);
    s.configSent(1000, true);
    int noPvt = 0;
    for (uint32_t t = 1000; t < 20000; t += 50) {
      CHECK(s.onNmea(t) == GpsEvent::None, "nmea at %u", t);
      if (s.poll(t) == GpsEvent::NoPvt) {
        noPvt++;
        CHECK(t > 4000 && t <= 4050, "gave up at %u", t);
      }
    }
    CHECK(noPvt == 1 && !s.pvtActive(), "no-pvt events %d", noPvt);
    // A slow module that starts NAV-PVT after all is still used
    CHECK(s.onPvt(20000) == GpsEvent::PvtStarted, "late pvt");
  }
  {  // exact boundaries: wait (3000) and lost (1000) are "more than"
    GpsSourceSwitch s(true, 3000);
    s.configSent(1001, true);
    CHECK(s.poll(4001) == GpsEvent::None, "at exactly the wait");
    CHECK(s.poll(4002) == GpsEvent::NoPvt, "just past the wait");
    s.onPvt(5000);
    CHECK(s.onNmea(6000) == GpsEvent::None, "at exactly the lost time");
    CHECK(s.onNmea(6001) == GpsEvent::PvtLost && !s.pvtActive(), "just past the lost time");
    CHECK(s.onNmea(6050) == GpsEvent::None, "lost reported once");
  }
  {  // GPS restarts: PVT stops, NMEA comes back
    GpsSourceSwitch s(true, 3000);
    s.configSent(0, true);
    s.onPvt(100);
    CHECK(s.onNmea(1100) == GpsEvent::None, "pvt recent");
    CHECK(s.onNmea(1150) == GpsEvent::PvtLost, "pvt lost");
    s.configSent(1150, true);
    CHECK(s.onPvt(1250) == GpsEvent::PvtStarted, "pvt back");
  }
  {  // a 1 Hz GPS with NMEA left on alongside PVT (GPS_ECHO_NMEA) isn't 'lost'
     // when GPSHandler sets the lost time to 3 fix intervals
    GpsSourceSwitch s(true, 3000, 3000);
    s.configSent(0, true);
    for (uint32_t t = 100; t < 20000; t += 1000) {
      s.onPvt(t);
      CHECK(s.onNmea(t + 990) == GpsEvent::None, "1 Hz at %u", t);
    }
  }
  {  // GPS unplugged while on PVT: silence alone isn't "lost", nothing to do
    GpsSourceSwitch s(true, 3000);
    s.configSent(0, true);
    s.onPvt(100);
    CHECK(s.poll(60000) == GpsEvent::None && s.pvtActive(), "silence");
  }
  {  // no GPS at start-up: no timeout while nothing talks; configure when NMEA appears
    GpsSourceSwitch s(true, 3000);
    s.configSent(500, false);
    CHECK(s.poll(10000) == GpsEvent::None, "no GPS: no 'not u-blox' message");
    CHECK(s.onNmea(10000) == GpsEvent::Configure, "late GPS: configure");
    CHECK(s.onNmea(10050) == GpsEvent::None, "configure once");
    s.configSent(10050, true);
    CHECK(s.poll(13100) == GpsEvent::NoPvt, "wait starts from the late config");
  }
  {  // a GPS already sending PVT counts as configured
    GpsSourceSwitch s(true, 3000);
    s.configSent(0, false);
    CHECK(s.onPvt(10) == GpsEvent::PvtStarted, "pvt first");
    CHECK(s.onNmea(20) == GpsEvent::None, "no configure after pvt");
  }
  {  // GPS_USE_UBX 0: NAV-PVT ignored
    GpsSourceSwitch s(false, 3000);
    s.configSent(0, true);
    CHECK(s.onPvt(10) == GpsEvent::None && !s.pvtActive(), "ubx off");
    CHECK(s.poll(10000) == GpsEvent::None, "no timeout message with ubx off");
  }
  {  // GPSHandler takes `now` at the start of update(), then the config is
     // sent (millis() a little later): a `now` just before must not time out
    GpsSourceSwitch s(true, 3000);
    s.configSent(1001, true);
    CHECK(s.poll(1000) == GpsEvent::None, "now 1 ms before the config");
    s.onPvt(2001);
    CHECK(s.onNmea(2000) == GpsEvent::None, "now 1 ms before the last PVT");
  }
  {  // millis() wrapping round (49.7 days)
    GpsSourceSwitch s(true, 3000);
    s.configSent(0xFFFFFC00u, true);
    CHECK(s.poll(0x00000100u) == GpsEvent::None, "wrap: within the wait");  // 1.3 s later
    CHECK(s.poll(0x00000C00u) == GpsEvent::NoPvt, "wrap: past the wait");   // 4.1 s later
    s.onPvt(0xFFFFFF00u);
    CHECK(s.onNmea(0x00000100u) == GpsEvent::None, "wrap: pvt recent");
    CHECK(s.onNmea(0x00000800u) == GpsEvent::PvtLost, "wrap: pvt lost");
  }
  printf("  source switch: ok\n");
}

static UbxPvt goodPvt() {
  UbxPvt v = {};
  v.year = 2026; v.month = 10; v.day = 14; v.hour = 12; v.minute = 0; v.second = 1; v.nano = 50000000;
  v.valid = 0x07;
  v.fixType = 3; v.flags = 0x01; v.numSV = 9;
  v.lat = 453017778; v.lon = -766715556; v.hMSL = 152500;
  v.hAcc = 1234; v.vAcc = 2100; v.sAcc = 180; v.headAcc = 52000; v.pDOP = 135;
  v.gSpeed = 2000; v.headMot = 27125000; v.velD = 250;
  return v;
}

static void testFixFromPvt() {
  GpsFix prev = {}, out = {};
  UbxPvt v = goodPvt();
  CHECK(fixFromPvt(v, 777, prev, out), "good fix");
  CHECK(out.gpsTimeMs == 43201050u && out.rxMillis == 777, "time %u", out.gpsTimeMs);
  CHECK(fabs(out.lat - 45.3017778) < 1e-9 && fabs(out.lon + 76.6715556) < 1e-9, "position");
  CHECK(fabs(out.speedKmh - 7.2f) < 1e-4 && out.courseValid, "speed %f", out.speedKmh);
  CHECK(fabs(out.courseDeg - 271.25f) < 1e-3 && fabs(out.altM - 152.5f) < 1e-3, "course/alt");
  CHECK(fabs(out.hAccM - 1.234f) < 1e-6 && fabs(out.vAccM - 2.1f) < 1e-6, "h/v acc");
  CHECK(fabs(out.speedAccMs - 0.18f) < 1e-6 && fabs(out.headingAccDeg - 0.52f) < 1e-6, "s/heading acc");
  CHECK(fabs(out.vertSpeedMs + 0.25f) < 1e-6 && fabs(out.pdop - 1.35f) < 1e-6, "vspeed/pdop");
  CHECK(out.fixType == 3 && out.sats == 9 && out.accuracyKnown(), "type/sats");
  CHECK(out.year == 2026 && out.month == 10 && out.day == 14, "date");

  // Same epoch again (or NMEA gave it just before the switch): skipped
  GpsFix again = out;
  CHECK(!fixFromPvt(v, 800, out, again), "duplicate epoch");
  // Same time of day but no previous fix: fine
  CHECK(fixFromPvt(v, 800, GpsFix{}, again), "first fix");

  // Slow: heading not trusted
  v.nano = 100000000;
  v.gSpeed = 800;  // 2.9 km/h
  CHECK(fixFromPvt(v, 900, out, again) && !again.courseValid, "slow heading");

  // No date this epoch: the previous one is kept
  v.nano = 150000000;
  v.valid = 0x02;
  v.year = 0; v.month = 0; v.day = 0;
  CHECK(fixFromPvt(v, 950, out, again) && again.year == 2026 && again.day == 14, "date kept");

  // Not usable: out untouched
  GpsFix untouched = {};
  untouched.lat = 1.0;
  UbxPvt bad = goodPvt();
  bad.flags = 0;
  CHECK(!fixFromPvt(bad, 1, prev, untouched) && untouched.lat == 1.0, "gnssFixOK clear");
  bad = goodPvt();
  bad.fixType = 5;
  CHECK(!fixFromPvt(bad, 1, prev, untouched), "time-only fix");
  bad = goodPvt();
  bad.flags3 = 0x01;
  CHECK(!fixFromPvt(bad, 1, prev, untouched), "invalid lat/lon");
  bad = goodPvt();
  bad.valid = 0x01;  // date but no time
  CHECK(!fixFromPvt(bad, 1, prev, untouched) && untouched.lat == 1.0, "time not valid");

  // Writing over the previous fix in place, as GPSHandler does
  GpsFix inplace = out;
  UbxPvt n = goodPvt();
  n.second = 2;
  CHECK(fixFromPvt(n, 2000, inplace, inplace) && inplace.gpsTimeMs == 43202050u, "in place");
  printf("  NAV-PVT to fix: ok\n");
}

// The encoder used by the simulation matches an independently built frame
// (see test_ubx.cpp), so the simulation's bytes are what a GPS sends.
static void testEncoder() {
  static const uint8_t PVT_MOVING[100] = {
      0xB5, 0x62, 0x01, 0x07, 0x5C, 0x00, 0x8C, 0x86, 0x26, 0x12, 0xEA, 0x07, 0x0A, 0x0E, 0x0C, 0x22,
      0x38, 0x07, 0x15, 0x00, 0x00, 0x00, 0xF3, 0x26, 0xB9, 0x29, 0x03, 0x01, 0xE0, 0x0B, 0x5C, 0xD9,
      0x4C, 0xD2, 0xB2, 0x80, 0x00, 0x1B, 0x74, 0xD0, 0x01, 0x00, 0xB4, 0x53, 0x02, 0x00, 0xD2, 0x04,
      0x00, 0x00, 0x34, 0x08, 0x00, 0x00, 0xD7, 0x02, 0x00, 0x00, 0xD3, 0x7D, 0xFF, 0xFF, 0xFA, 0x00,
      0x00, 0x00, 0x35, 0x82, 0x00, 0x00, 0x08, 0xE5, 0x9D, 0x01, 0xB4, 0x00, 0x00, 0x00, 0x20, 0xCB,
      0x00, 0x00, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0xAC, 0x4E,
  };
  UbxPvt v;
  CHECK(decodeNavPvt(PVT_MOVING + 6, 92, v), "decode sample");
  std::vector<uint8_t> f = encodePvt(v);
  CHECK(memcmp(f.data(), PVT_MOVING, 100) == 0, "encoder differs from the sample");
}

// ---------------------------------------------------------------------------
// End to end

struct RunResult {
  int laps = 0;
  double worstLapErrMs = 0;
};

// Drives `seconds` of 20 Hz epochs. hook(i) can change the GPS mid-run.
template <typename Hook>
static RunResult run(SimGps& g, Receiver& r, double seconds, bool stalls, Hook hook, uint32_t t0 = 5000) {
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15}, {});
  Link link;
  size_t fed = 0;
  RunResult res;
  const double expectedMs = 2 * M_PI * RADIUS / SPEED * 1000.0;
  int epochs = (int)(seconds * 20);
  for (int i = 0; i < epochs; i++) {
    uint32_t now = t0 + 50u * i;
    hook(i);
    link.send(g.epoch(i));
    link.deliver(r, now + 3, stalls);
    for (; fed < r.fixes.size(); fed++) {
      const GpsFix& f = r.fixes[fed];
      LapEvent e = lt.onFix(f.lat, f.lon, f.speedKmh, f.gpsTimeMs, f.rxMillis);
      if (e.type == LapEventType::LapCompleted) {
        res.laps++;
        double err = fabs(e.timeMs - expectedMs);
        if (err > res.worstLapErrMs) res.worstLapErrMs = err;
      }
    }
  }
  link.deliver(r, t0 + 50u * epochs + 2000, false);
  return res;
}

static bool fixesEvery50ms(const std::vector<GpsFix>& fixes, size_t from) {
  for (size_t i = from + 1; i < fixes.size(); i++) {
    uint32_t d = (fixes[i].gpsTimeMs + 86400000u - fixes[i - 1].gpsTimeMs) % 86400000u;
    if (d != 50) {
      printf("    gap of %u ms at fix %zu\n", d, i);
      return false;
    }
  }
  return true;
}

static void testEndToEnd() {
  const double lapS = 2 * M_PI * RADIUS / SPEED;  // 31.4 s
  testSeed(7);

  {  // u-blox, fresh power-up (NMEA only), loop stalls up to 1.2 s
    SimGps g;
    Receiver r;
    r.gps = &g;
    r.begin(4990, true);
    RunResult res = run(g, r, lapS * 4.6, true, [](int) {});
    CHECK(r.count(GpsEvent::PvtStarted) == 1 && r.count(GpsEvent::NoPvt) == 0 &&
          r.count(GpsEvent::PvtLost) == 0, "events %d %d %d", r.count(GpsEvent::PvtStarted),
          r.count(GpsEvent::NoPvt), r.count(GpsEvent::PvtLost));
    CHECK(r.configs == 1 && r.nmeaOffs == 1 && !g.nmeaOn && g.pvtOn, "gps left on PVT only");
    int epochs = (int)(lapS * 4.6 * 20);
    CHECK((int)r.fixes.size() >= epochs - 2, "fixes %zu of %d epochs", r.fixes.size(), epochs);
    CHECK(fixesEvery50ms(r.fixes, 0), "a fix every 50 ms, none lost or repeated");
    CHECK(res.laps == 4 && res.worstLapErrMs <= 2.0, "laps %d worst error %.1f ms", res.laps, res.worstLapErrMs);
    CHECK(r.fixes.back().accuracyKnown() && fabs(r.fixes.back().hAccM - 0.85f) < 1e-6, "accuracy");
    CHECK(r.ubx.checksumErrors() == 0, "checksum errors %u", (unsigned)r.ubx.checksumErrors());
    printf("  u-blox from power-up, stalling main loop: %d laps, worst error %.1f ms, %zu fixes\n", res.laps,
           res.worstLapErrMs, r.fixes.size());
  }

  {  // laps across UTC midnight
    SimGps g;
    g.startUtcMs = 86400000u - 45000u;
    Receiver r;
    r.gps = &g;
    r.begin(4990, true);
    RunResult res = run(g, r, lapS * 3.6, false, [](int) {});
    CHECK(res.laps == 3 && res.worstLapErrMs <= 2.0, "midnight: laps %d worst %.1f", res.laps, res.worstLapErrMs);
    CHECK(fixesEvery50ms(r.fixes, 0), "midnight: fixes every 50 ms");
    printf("  laps across UTC midnight: %d laps, worst error %.1f ms\n", res.laps, res.worstLapErrMs);
  }

  {  // not u-blox: ignores UBX, NMEA only
    SimGps g;
    g.ublox = false;
    Receiver r;
    r.gps = &g;
    r.begin(4990, true);
    run(g, r, 10, true, [](int) {});
    CHECK(r.count(GpsEvent::NoPvt) == 1 && r.count(GpsEvent::PvtStarted) == 0 && !r.sw.pvtActive(),
          "not u-blox: events");
    CHECK(r.fixes.empty() && g.nmeaOn, "not u-blox: stays on NMEA");
    printf("  not u-blox: stays on NMEA, one 'no UBX-NAV-PVT' message\n");
  }

  {  // ESP32 reset while the GPS kept power: PVT only from the start
    SimGps g;
    g.nmeaOn = false;
    g.pvtOn = true;
    Receiver r;
    r.gps = &g;
    r.begin(4990, true);
    run(g, r, 10, false, [](int) {});
    CHECK(r.count(GpsEvent::PvtStarted) == 1 && r.count(GpsEvent::NoPvt) == 0, "reset: events");
    CHECK(!g.nmeaOn && fixesEvery50ms(r.fixes, 0) && r.fixes.size() >= 198, "reset: %zu fixes", r.fixes.size());
    printf("  ESP32 reset, GPS still on PVT: %zu fixes\n", r.fixes.size());
  }

  {  // GPS restarts mid-session: 1 s of silence, then factory NMEA
    SimGps g;
    Receiver r;
    r.gps = &g;
    r.begin(4990, true);
    size_t before = 0;
    run(g, r, 30, false, [&](int i) {
      if (i == 200) { g.silent = true; before = r.fixes.size(); }
      if (i == 220) { g.silent = false; g.restart(); }
    });
    CHECK(r.count(GpsEvent::PvtLost) == 1 && r.count(GpsEvent::PvtStarted) == 2, "restart: lost %d started %d",
          r.count(GpsEvent::PvtLost), r.count(GpsEvent::PvtStarted));
    CHECK(r.configs == 2 && r.nmeaOffs == 2 && !g.nmeaOn && g.pvtOn && r.sw.pvtActive(), "restart: back on PVT");
    CHECK(r.fixes.size() > before + 300, "restart: fixes resume (%zu then %zu)", before, r.fixes.size());
    uint32_t worstGap = 0;
    for (size_t i = 1; i < r.fixes.size(); i++)
      if (r.fixes[i].gpsTimeMs - r.fixes[i - 1].gpsTimeMs > worstGap) worstGap = r.fixes[i].gpsTimeMs - r.fixes[i - 1].gpsTimeMs;
    // 1 s of silence, then NMEA: PVT asked for and back within a few epochs
    CHECK(worstGap <= 1200, "restart: fixes stopped for %u ms", worstGap);
    printf("  GPS restart mid-session: back on PVT, %zu fixes\n", r.fixes.size());
  }

  {  // no GPS at start-up, plugged in after 8 s
    SimGps g;
    g.silent = true;
    Receiver r;
    r.gps = &g;
    r.begin(4990, false);
    run(g, r, 20, false, [&](int i) {
      if (i == 160) { g.silent = false; g.restart(); }
    });
    CHECK(r.count(GpsEvent::NoPvt) == 0, "late GPS: no false 'not u-blox' message");
    CHECK(r.count(GpsEvent::Configure) == 1 && r.count(GpsEvent::PvtStarted) == 1 && r.configs == 2,
          "late GPS: configured %d started %d", r.count(GpsEvent::Configure), r.count(GpsEvent::PvtStarted));
    CHECK(r.fixes.size() > 150 && !g.nmeaOn, "late GPS: %zu fixes", r.fixes.size());
    printf("  GPS plugged in late: configured, %zu fixes\n", r.fixes.size());
  }

  {  // GPS_USE_UBX 0: the base config turns PVT off; any PVT is ignored
    SimGps g;
    g.useUbx = false;
    g.nmeaOn = true;
    g.pvtOn = true;  // left on by an earlier firmware
    Receiver r(false);
    r.gps = &g;
    r.begin(4990, true);
    run(g, r, 6, false, [](int) {});
    CHECK(r.count(GpsEvent::PvtStarted) == 0 && r.fixes.empty() && !g.pvtOn && g.nmeaOn, "ubx off");
    printf("  GPS_USE_UBX 0: NMEA only\n");
  }

  {  // corrupted bytes: bad frames dropped, the rest still used
    SimGps g;
    Receiver r;
    r.gps = &g;
    r.begin(4990, true);
    Link link;
    int corrupted = 0;
    for (int i = 0; i < 400; i++) {
      std::vector<uint8_t> b = g.epoch(i);
      if (i > 20 && i % 37 == 0 && b.size() >= 100) {
        b[b.size() - 60] ^= 0x10;  // one bit in the NAV-PVT payload
        corrupted++;
      }
      link.send(b);
      link.deliver(r, 5000 + 50u * i + 3, false);
    }
    CHECK(r.ubx.checksumErrors() == (uint32_t)corrupted, "checksum errors %u of %d", (unsigned)r.ubx.checksumErrors(),
          corrupted);
    CHECK(r.count(GpsEvent::PvtLost) == 0 && r.sw.pvtActive(), "corruption: still on PVT");
    CHECK(r.fixes.size() >= (size_t)(400 - 3 - corrupted), "corruption: %zu fixes", r.fixes.size());
    printf("  corrupted frames: %d dropped, %zu fixes kept\n", corrupted, r.fixes.size());
  }

  {  // no satellites: PVT arrives without a fix, then the fix comes
    SimGps g;
    Receiver r;
    r.gps = &g;
    r.begin(4990, true);
    Link link;
    for (int i = 0; i < 200; i++) {
      link.send(g.epoch(i, i >= 120));
      link.deliver(r, 5000 + 50u * i + 3, false);
    }
    CHECK(r.sw.pvtActive() && r.count(GpsEvent::NoPvt) == 0, "no fix: PVT still counts as arriving");
    CHECK(r.fixes.size() == 80 && fixesEvery50ms(r.fixes, 0), "no fix: %zu fixes", r.fixes.size());
    printf("  waiting for satellites: no fixes until there's a fix, then %zu\n", r.fixes.size());
  }
}

int main() {
  printf("GPS source tests\n");
  testSwitchUnits();
  testFixFromPvt();
  testEncoder();
  testEndToEnd();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
