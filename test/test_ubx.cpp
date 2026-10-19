/**
 * Host-side tests for the UBX reader and the NAV-PVT decoder.
 *
 *   g++ -std=c++17 -Iinclude test/test_ubx.cpp src/UbxParser.cpp -o /tmp/ubx && /tmp/ubx
 */

#include <cmath>
#include <cstdio>
#include <cstring>
#include "UbxParser.h"

static int failures = 0;
#define CHECK(cond, ...)                         \
  do {                                           \
    if (!(cond)) {                               \
      failures++;                                \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                       \
      printf("\n");                              \
    }                                            \
  } while (0)

// Frames written out byte by byte, with checksums worked out separately
// from the code under test. Layout as in the u-blox M10 interface manual.

// Moving at 120 km/h near Calabogie: Wed 2026-10-14 12:34:56.699999987 UTC,
// 3D fix, 11 satellites, +/-1.234 m, +/-0.18 m/s, going down at 0.25 m/s.
static const uint8_t PVT_MOVING[100] = {
    0xB5, 0x62, 0x01, 0x07, 0x5C, 0x00, 0x8C, 0x86, 0x26, 0x12, 0xEA, 0x07, 0x0A, 0x0E, 0x0C, 0x22,
    0x38, 0x07, 0x15, 0x00, 0x00, 0x00, 0xF3, 0x26, 0xB9, 0x29, 0x03, 0x01, 0xE0, 0x0B, 0x5C, 0xD9,
    0x4C, 0xD2, 0xB2, 0x80, 0x00, 0x1B, 0x74, 0xD0, 0x01, 0x00, 0xB4, 0x53, 0x02, 0x00, 0xD2, 0x04,
    0x00, 0x00, 0x34, 0x08, 0x00, 0x00, 0xD7, 0x02, 0x00, 0x00, 0xD3, 0x7D, 0xFF, 0xFF, 0xFA, 0x00,
    0x00, 0x00, 0x35, 0x82, 0x00, 0x00, 0x08, 0xE5, 0x9D, 0x01, 0xB4, 0x00, 0x00, 0x00, 0x20, 0xCB,
    0x00, 0x00, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xAC, 0x4E,
};

// Just switched on: no fix, date and time not valid, lat/lon flagged invalid.
static const uint8_t PVT_NO_FIX[100] = {
    0xB5, 0x62, 0x01, 0x07, 0x5C, 0x00, 0xE8, 0x03, 0x00, 0x00, 0xDF, 0x07, 0x01, 0x01, 0x00, 0x00,
    0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xF4, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x4E, 0x00, 0x00, 0x80, 0xA8,
    0x12, 0x01, 0x0F, 0x27, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFD, 0x22,
};

// UBX-ACK-ACK for a CFG-VALSET, as an M10 sends it.
static const uint8_t ACK_VALSET[10] = {0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x06, 0x8A, 0x98, 0xC1};

static const char NMEA_GGA[] =
    "$GNGGA,123456.70,4518.10667,N,07640.29334,W,1,11,0.90,152.5,M,-33.6,M,,*75\r\n";

// Feeds bytes; returns how many frames completed (checksum good).
static int feedAll(UbxParser& p, const uint8_t* b, size_t n) {
  int frames = 0;
  for (size_t i = 0; i < n; i++)
    if (p.feed(b[i])) frames++;
  return frames;
}

static bool near(double a, double b, double tol) { return fabs(a - b) <= tol; }

int main() {
  printf("UBX tests\n");

  // --- Checksums ---
  {
    UbxParser p;
    CHECK(!p.inFrame(), "idle");
    CHECK(feedAll(p, ACK_VALSET, 1) == 0 && p.inFrame(), "in frame after sync byte");
    CHECK(feedAll(p, ACK_VALSET + 1, 8) == 0 && p.inFrame(), "in frame before the last byte");
    CHECK(feedAll(p, ACK_VALSET + 9, 1) == 1 && !p.inFrame(), "frame done");
    CHECK(p.is(0x05, 0x01) && p.length() == 2 && p.payload()[0] == 0x06 && p.payload()[1] == 0x8A,
          "ack contents %02X %02X len %u", p.msgClass(), p.msgId(), p.length());

    uint8_t bad[sizeof(PVT_MOVING)];
    memcpy(bad, PVT_MOVING, sizeof(bad));
    bad[50] ^= 0x01;  // one bit flipped in the payload
    CHECK(feedAll(p, bad, sizeof(bad)) == 0, "corrupt payload accepted");
    memcpy(bad, PVT_MOVING, sizeof(bad));
    bad[99] ^= 0x80;  // checksum byte B wrong
    CHECK(feedAll(p, bad, sizeof(bad)) == 0, "bad checksum B accepted");
    memcpy(bad, PVT_MOVING, sizeof(bad));
    bad[98] ^= 0x01;  // checksum byte A wrong
    CHECK(feedAll(p, bad, sizeof(bad)) == 0, "bad checksum A accepted");
    CHECK(p.checksumErrors() == 3, "checksum errors %u", (unsigned)p.checksumErrors());

    // Still in step afterwards
    CHECK(feedAll(p, PVT_MOVING, sizeof(PVT_MOVING)) == 1, "good frame after bad ones");
    CHECK(p.framesOk() == 2, "frames ok %u", (unsigned)p.framesOk());
    printf("  checksums: ok\n");
  }

  // --- NAV-PVT field decoding ---
  {
    UbxParser p;
    CHECK(feedAll(p, PVT_MOVING, sizeof(PVT_MOVING)) == 1, "pvt frame");
    CHECK(p.is(UBX_CLASS_NAV, UBX_ID_NAV_PVT) && p.length() == UBX_NAV_PVT_LEN, "pvt header");
    UbxPvt v;
    CHECK(decodeNavPvt(p.payload(), p.length(), v), "decode");
    CHECK(v.iTOW == 304514700, "iTOW %u", v.iTOW);
    CHECK(v.year == 2026 && v.month == 10 && v.day == 14, "date %u-%u-%u", v.year, v.month, v.day);
    CHECK(v.hour == 12 && v.minute == 34 && v.second == 56, "time %u:%u:%u", v.hour, v.minute, v.second);
    CHECK(v.dateValid() && v.timeValid(), "valid %02X", v.valid);
    CHECK(v.tAcc == 21, "tAcc %u", v.tAcc);
    CHECK(v.nano == 699999987, "nano %d", v.nano);
    CHECK(v.utcMsOfDay() == 45296700u, "ms of day %u", v.utcMsOfDay());  // 12:34:56.700
    CHECK(v.fixType == 3 && v.fixOk(), "fix %u ok %d", v.fixType, v.fixOk());
    CHECK(v.numSV == 11, "numSV %u", v.numSV);
    CHECK(v.lat == 453017778 && v.lon == -766715556, "lat/lon %d %d", v.lat, v.lon);
    CHECK(near(v.latDeg(), 45.3017778, 1e-9) && near(v.lonDeg(), -76.6715556, 1e-9), "degrees");
    CHECK(v.height == 118900 && v.hMSL == 152500 && near(v.altMslM(), 152.5, 1e-3), "heights");
    CHECK(v.hAcc == 1234 && near(v.hAccM(), 1.234, 1e-6), "hAcc %u", v.hAcc);
    CHECK(v.vAcc == 2100 && near(v.vAccM(), 2.1, 1e-6), "vAcc %u", v.vAcc);
    CHECK(v.velN == 727 && v.velE == -33325 && v.velD == 250, "vel %d %d %d", v.velN, v.velE, v.velD);
    CHECK(near(v.vertSpeedMs(), -0.25, 1e-6), "vertical speed %f", v.vertSpeedMs());
    CHECK(v.gSpeed == 33333 && near(v.speedMs() * 3.6, 120.0, 0.01), "speed %d", v.gSpeed);
    CHECK(v.headMot == 27125000 && near(v.headingDeg(), 271.25, 1e-3), "heading %d", v.headMot);
    CHECK(v.sAcc == 180 && near(v.speedAccMs(), 0.18, 1e-6), "sAcc %u", v.sAcc);
    CHECK(v.headAcc == 52000 && near(v.headingAccDeg(), 0.52, 1e-5), "headAcc %u", v.headAcc);
    CHECK(v.pDOP == 135 && near(v.pdop(), 1.35, 1e-5), "pDOP %u", v.pDOP);
    CHECK(v.flags3 == 0, "flags3");

    // Speed from the velocity vector agrees with gSpeed
    CHECK(near(sqrt((double)v.velN * v.velN + (double)v.velE * v.velE), v.gSpeed, 1.0), "velocity vector");
    CHECK(!decodeNavPvt(p.payload(), 84, v), "short payload accepted");
    printf("  NAV-PVT fields: ok\n");

    CHECK(feedAll(p, PVT_NO_FIX, sizeof(PVT_NO_FIX)) == 1, "no-fix frame");
    CHECK(decodeNavPvt(p.payload(), p.length(), v), "decode no-fix");
    CHECK(!v.fixOk() && v.fixType == 0 && v.numSV == 0, "no-fix flags");
    CHECK(!v.dateValid() && !v.timeValid(), "no-fix validity %02X", v.valid);
    CHECK(v.hAcc == 0xFFFFFFFF && v.nano == -12, "no-fix hAcc/nano");
    // 00:00:01 minus 12 ns rounds to exactly 1 s
    CHECK(v.utcMsOfDay() == 1000, "no-fix ms of day %u", v.utcMsOfDay());
    printf("  NAV-PVT without a fix: ok\n");
  }

  // --- A 3D fix with lat/lon flagged invalid, or a time-only fix, isn't usable ---
  {
    UbxPvt v = {};
    v.flags = 0x01;
    v.fixType = 3;
    CHECK(v.fixOk(), "3D");
    v.fixType = 2;
    CHECK(v.fixOk(), "2D");
    v.fixType = 5;
    CHECK(!v.fixOk(), "time only");
    v.fixType = 1;
    CHECK(!v.fixOk(), "dead reckoning only");
    v.fixType = 3;
    v.flags3 = 0x01;
    CHECK(!v.fixOk(), "invalidLlh");
    v.flags3 = 0;
    v.flags = 0;
    CHECK(!v.fixOk(), "gnssFixOK clear");
  }

  // --- Millisecond time ---
  {
    CHECK(utcMsOfDay(12, 34, 56, 700000000) == 45296700u, "exact");
    CHECK(utcMsOfDay(12, 34, 56, 49999987) == 45296050u, "20 Hz epoch, rounds up");
    CHECK(utcMsOfDay(12, 34, 57, -13) == 45297000u, "small negative nano");
    CHECK(utcMsOfDay(12, 34, 57, -600000) == 45296999u, "negative nano rounds down a ms");
    CHECK(utcMsOfDay(0, 0, 0, -600000) == 86399999u, "wraps before midnight");
    CHECK(utcMsOfDay(23, 59, 59, 999600000) == 0u, "wraps after midnight");
    CHECK(utcMsOfDay(23, 59, 60, 0) == 0u, "leap second");
    printf("  millisecond time: ok\n");
  }

  // --- Frames split across reads, mixed with NMEA ---
  {
    // Stream: NMEA, PVT, NMEA, no-fix PVT, ACK, with a stray sync byte
    uint8_t stream[600];
    size_t n = 0;
    auto put = [&](const void* b, size_t len) { memcpy(stream + n, b, len); n += len; };
    put(NMEA_GGA, strlen(NMEA_GGA));
    put(PVT_MOVING, sizeof(PVT_MOVING));
    put(NMEA_GGA, strlen(NMEA_GGA));
    const uint8_t stray = 0xB5;
    put(&stray, 1);
    put(PVT_NO_FIX, sizeof(PVT_NO_FIX));
    put(ACK_VALSET, sizeof(ACK_VALSET));

    // Every split size from 1 byte at a time up to the whole stream in one
    // go, as UART reads would deliver it.
    for (size_t chunk = 1; chunk <= n; chunk++) {
      UbxParser p;
      int pvts = 0, acks = 0;
      bool movingOk = false;
      for (size_t off = 0; off < n; off += chunk) {
        size_t len = off + chunk <= n ? chunk : n - off;
        for (size_t i = 0; i < len; i++) {
          if (!p.feed(stream[off + i])) continue;
          UbxPvt v;
          if (p.is(UBX_CLASS_NAV, UBX_ID_NAV_PVT) && decodeNavPvt(p.payload(), p.length(), v)) {
            if (pvts == 0) movingOk = v.fixOk() && v.lat == 453017778 && v.gSpeed == 33333;
            pvts++;
          } else if (p.is(0x05, 0x01)) {
            acks++;
          }
        }
      }
      CHECK(pvts == 2 && acks == 1 && movingOk, "chunk %u: %d pvt %d ack moving=%d", (unsigned)chunk, pvts,
            acks, movingOk);
      CHECK(p.checksumErrors() == 0, "chunk %u: checksum errors", (unsigned)chunk);
    }
    printf("  split frames and NMEA between them: ok\n");
  }

  // --- A frame too big for the buffer is skipped, and the next one is read ---
  {
    UbxParser p;
    const uint8_t big[] = {0xB5, 0x62, 0x01, 0x35, 0x00, 0x02};  // NAV-SAT, 512-byte payload
    CHECK(feedAll(p, big, sizeof(big)) == 0, "big frame");
    for (int i = 0; i < 514; i++) p.feed((uint8_t)(i * 7));  // its payload: arbitrary bytes
    CHECK(feedAll(p, PVT_MOVING, sizeof(PVT_MOVING)) == 1, "frame after a big one");
    printf("  oversized frames: ok\n");
  }

  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
