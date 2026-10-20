/**
 * Track Rat - the DJI camera remote protocol (6.2): Track Rat as the GPS
 * remote of an Osmo Action 4, 5 Pro, 6 or Osmo 360
 *
 * Ported from the protocol layer of DJI's Osmo GPS Controller Demo
 * (https://github.com/dji-sdk/Osmo-GPS-Controller-Demo: protocol/, utils/crc/,
 * logic/connect_logic.c), which runs on an ESP32-C6 under ESP-IDF. What's kept:
 * the frame (SOF, Ver/Length, CmdType, ENC, RES, SEQ, CRC-16, DATA, CRC-32,
 * little-endian), its two CRCs (seeded 0x3AA3), the payloads of the connection
 * request (0x00/0x19), the GPS push (0x00/0x17), record control (0x1D/0x03),
 * the status subscription (0x1D/0x05) and status push (0x1D/0x02), and the
 * connection handshake. Rewritten in Track Rat's style with no heap and no
 * ESP-IDF, checked against the demo's own frames in test/test_dji.cpp.
 *
 * The DJI R SDK protocol is subject to DJI's licence terms
 * (http://developer.dji.com/policies/eula/). The demo's code carries this notice:
 *
 *   SPDX-License-Identifier: MIT
 *   Copyright (C) 2025 SZ DJI Technology Co., Ltd.
 *
 *   The MIT License (MIT)
 *   Permission is hereby granted, free of charge, to any person obtaining a copy
 *   of this software and associated documentation files (the "Software"), to deal
 *   in the Software without restriction, including without limitation the rights
 *   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *   copies of the Software, and to permit persons to whom the Software is
 *   furnished to do so, subject to the following conditions:
 *   The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *   SOFTWARE.
 *
 * Pure C++ (no Arduino). Untested with a camera: the user has an Action 4.
 */

#ifndef DJI_REMOTE_H
#define DJI_REMOTE_H

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace dji {

// --- the frame -----------------------------------------------------------------

constexpr uint8_t SOF = 0xAA;
constexpr int HEADER = 12;                  // SOF .. CRC-16
constexpr int OVERHEAD = HEADER + 2 + 4;    // + CmdSet, CmdID and CRC-32
constexpr int MAX_FRAME = 0x3FF;            // 10 bits of length

// CmdType: bits 4:0 the response wanted, bit 5 a response frame
constexpr uint8_t CMD_NO_RESPONSE = 0x00, CMD_RESPONSE_OR_NOT = 0x01, CMD_WAIT_RESULT = 0x02;
constexpr uint8_t ACK_NO_RESPONSE = 0x20;
constexpr uint8_t RESPONSE_BIT = 0x20;

// CmdSet / CmdID
constexpr uint8_t SET_GENERAL = 0x00, ID_GPS = 0x17, ID_CONNECT = 0x19;
constexpr uint8_t SET_CAMERA = 0x1D, ID_STATUS_PUSH = 0x02, ID_RECORD = 0x03, ID_SUBSCRIBE = 0x05;

// The demo's CRCs: the usual reflected polynomials, seeded 0x3AA3, no final XOR
inline uint16_t crc16(const uint8_t* d, int n) {
  uint16_t c = 0x3AA3;
  while (n--) {
    c ^= *d++;
    for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xA001 : c >> 1;
  }
  return c;
}
inline uint32_t crc32(const uint8_t* d, int n) {
  uint32_t c = 0x3AA3;
  while (n--) {
    c ^= *d++;
    for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
  }
  return c;
}

inline void put16(uint8_t* p, uint16_t v) { p[0] = v, p[1] = v >> 8; }
inline void put32(uint8_t* p, uint32_t v) { p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24; }
inline uint16_t get16(const uint8_t* p) { return p[0] | p[1] << 8; }
inline uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
inline void putFloat(uint8_t* p, float f) {
  uint32_t v;
  memcpy(&v, &f, 4);
  put32(p, v);
}

// A whole frame into out (at least OVERHEAD + len bytes). Returns its length, or 0.
inline int buildFrame(uint8_t cmdSet, uint8_t cmdId, uint8_t cmdType, const uint8_t* payload, int len, uint16_t seq, uint8_t* out, int cap) {
  const int total = OVERHEAD + len;
  if (len < 0 || total > MAX_FRAME || total > cap) return 0;
  out[0] = SOF;
  put16(out + 1, (uint16_t)(total & 0x3FF));  // version 0 in bits 15:10
  out[3] = cmdType;
  out[4] = 0;  // no encryption
  out[5] = out[6] = out[7] = 0;
  put16(out + 8, seq);
  put16(out + 10, crc16(out, 10));
  out[12] = cmdSet;
  out[13] = cmdId;
  if (len) memcpy(out + 14, payload, len);
  put32(out + 14 + len, crc32(out, 14 + len));
  return total;
}

struct Frame {
  uint8_t cmdType = 0, cmdSet = 0, cmdId = 0;
  uint16_t seq = 0;
  const uint8_t* payload = nullptr;  // after CmdSet and CmdID (points into the checked bytes)
  int len = 0;
  bool response() const { return cmdType & RESPONSE_BIT; }
};

// Checks one whole frame (length, both CRCs). False if it isn't one.
inline bool parseFrame(const uint8_t* d, int n, Frame& f) {
  if (n < OVERHEAD || d[0] != SOF) return false;
  if ((get16(d + 1) & 0x3FF) != n) return false;
  if (get16(d + 10) != crc16(d, 10)) return false;
  if (get32(d + n - 4) != crc32(d, n - 4)) return false;
  f.cmdType = d[3];
  f.seq = get16(d + 8);
  f.cmdSet = d[12];
  f.cmdId = d[13];
  f.payload = d + 14;
  f.len = n - OVERHEAD;
  return true;
}

// Puts frames back together from Bluetooth notifications (a frame may come in
// pieces, or two in one). Bytes before a SOF, and frames that fail their CRCs,
// are skipped.
class Assembler {
public:
  // Adds bytes. Then call next() until it returns false.
  void feed(const uint8_t* d, int n) {
    for (int i = 0; i < n; i++) {
      if (len == (int)sizeof(buf)) drop(1);  // full of rubbish: let it go
      buf[len++] = d[i];
    }
  }
  // The next whole frame, copied into out (at least MAX_FRAME bytes); f points into out.
  bool next(uint8_t* out, Frame& f) {
    for (;;) {
      int s = 0;
      while (s < len && buf[s] != SOF) s++;
      drop(s);
      if (len < HEADER) return false;
      const int n = get16(buf + 1) & 0x3FF;
      if (n < OVERHEAD || get16(buf + 10) != crc16(buf, 10)) {  // a stray 0xAA, not a header
        drop(1);
        continue;
      }
      if (len < n) return false;  // the rest is on its way
      memcpy(out, buf, n);
      if (parseFrame(out, n, f)) {
        drop(n);
        return true;
      }
      drop(1);  // not a frame after all: look for the next SOF
    }
  }
  void clear() { len = 0; }

private:
  uint8_t buf[2 * MAX_FRAME];
  int len = 0;
  void drop(int n) {
    if (n <= 0) return;
    if (n > len) n = len;
    memmove(buf, buf + n, len - n);
    len -= n;
  }
};

// --- payloads ------------------------------------------------------------------

constexpr uint32_t REMOTE_DEVICE_ID = 0x12345678;  // what the demo sends as its own id
constexpr uint32_t CAMERA_DEVICE_ID = 0x33FF0000;  // what the demo puts in record control

// Connection request (0x00/0x19), 33 bytes. verifyMode 1: the camera asks the
// user, showing verifyData; 0: it decides from its pairing history.
constexpr int CONNECT_LEN = 33;
inline void connectRequest(uint32_t deviceId, const uint8_t mac[6], uint8_t verifyMode, uint16_t verifyData, uint8_t out[CONNECT_LEN]) {
  memset(out, 0, CONNECT_LEN);
  put32(out, deviceId);
  out[4] = 6;
  memcpy(out + 5, mac, 6);  // mac[16]
  put32(out + 21, 0);       // our firmware version: 0, so the camera offers no updates
  out[25] = 0;              // conidx
  out[26] = verifyMode;
  put16(out + 27, verifyData);
}

// Our answer to the camera's own connection request: device id, 0 = fine, the camera's number (0: just one)
constexpr int CONNECT_REPLY_LEN = 9;
inline void connectReply(uint32_t deviceId, uint8_t out[CONNECT_REPLY_LEN]) {
  memset(out, 0, CONNECT_REPLY_LEN);
  put32(out, deviceId);
}

// Record control (0x1D/0x03): start or stop
constexpr int RECORD_LEN = 9;
inline void recordControl(bool start, uint8_t out[RECORD_LEN]) {
  memset(out, 0, RECORD_LEN);
  put32(out, CAMERA_DEVICE_ID);
  out[4] = start ? 0 : 1;
}

// Status subscription (0x1D/0x05): periodic and on every change (3), at 2 Hz (20: the only rate allowed)
constexpr int SUBSCRIBE_LEN = 6;
inline void subscribe(uint8_t out[SUBSCRIBE_LEN]) {
  memset(out, 0, SUBSCRIBE_LEN);
  out[0] = 3;
  out[1] = 20;
}

// GPS push (0x00/0x17), 48 bytes, from Track Rat's fix.
struct GpsData {
  uint16_t year = 0;
  uint8_t month = 0, day = 0;
  uint32_t utcMs = 0;  // since midnight
  double lat = 0, lon = 0;
  float heightM = 0;
  float speedKmh = 0, courseDeg = 0, climbMs = 0;
  float hAccM = 0, vAccM = 0, speedAccMs = 0;  // < 0: not known
  uint8_t sats = 0;
};
constexpr int GPS_LEN = 48;
// hourOffset: the demo adds 8 to the hour (China time), with no wrap past 24.
// DJI_HOUR_OFFSET in Config.h, until a camera shows what it wants.
inline void gpsPush(const GpsData& g, int hourOffset, uint8_t out[GPS_LEN]) {
  const uint32_t s = g.utcMs / 1000;
  put32(out + 0, (uint32_t)(g.year * 10000 + g.month * 100 + g.day));
  put32(out + 4, (uint32_t)((int)(s / 3600 + hourOffset) * 10000 + (s / 60 % 60) * 100 + s % 60));
  put32(out + 8, (uint32_t)(int32_t)lround(g.lon * 1e7));
  put32(out + 12, (uint32_t)(int32_t)lround(g.lat * 1e7));
  put32(out + 16, (uint32_t)(int32_t)lroundf(g.heightM * 1000));
  const float cms = g.speedKmh / 3.6f * 100.0f, rad = g.courseDeg * 3.14159265f / 180.0f;
  putFloat(out + 20, cms * cosf(rad));  // north
  putFloat(out + 24, cms * sinf(rad));  // east
  putFloat(out + 28, -g.climbMs * 100.0f);  // down
  auto acc = [](float v, float scale) { return v < 0 ? 99999u : (uint32_t)lroundf(v * scale); };  // not known: about 100 m
  put32(out + 32, acc(g.vAccM, 1000));
  put32(out + 36, acc(g.hAccM, 1000));
  put32(out + 40, acc(g.speedAccMs, 100));
  put32(out + 44, g.sats);
}

// The camera's status push (0x1D/0x02)
struct Status {
  bool known = false;
  uint8_t mode = 0, status = 0;
  uint16_t recordS = 0;  // the recording's length so far
  uint8_t tempOver = 0;  // 0 fine, 1 warm, 2 too hot to record, 3 about to shut down
  uint8_t battery = 0;   // %
  bool recording() const { return known && (status == 0x03 || status == 0x05); }  // recording, or pre-recording
};
constexpr int STATUS_MIN_LEN = 38;
inline bool parseStatus(const uint8_t* p, int len, Status& s) {
  if (len < STATUS_MIN_LEN) return false;
  s.known = true;
  s.mode = p[0];
  s.status = p[1];
  s.recordS = get16(p + 5);
  s.tempOver = p[30];
  s.battery = p[37] > 100 ? 100 : p[37];
  return true;
}

// A DJI camera's advertisement: manufacturer data AA 08 .. .. FA (the demo's test)
inline bool isCamera(const uint8_t* mfr, int len) { return len >= 5 && mfr[0] == 0xAA && mfr[1] == 0x08 && mfr[4] == 0xFA; }

// --- the conversation ------------------------------------------------------------

constexpr uint32_t REPLY_WAIT_MS = 1000;     // the camera answers our request straight away...
constexpr uint32_t CONFIRM_WAIT_MS = 30000;  // ...then sends its own once the user has accepted (or at once if paired)
constexpr uint32_t RECORD_RETRY_MS = 4000;   // a start or stop the status doesn't show yet: send it again
constexpr int RECORD_TRIES = 3;

// One frame to send (the Bluetooth side writes it, and empties the queue)
struct Out {
  uint8_t data[OVERHEAD + GPS_LEN];
  int len = 0;
};

class Remote {
public:
  enum class Phase : uint8_t { Off, Asking, Waiting, Connected, Rejected, Failed };

  // Connected over Bluetooth: ask the camera. pair: first time (the camera shows
  // `code` for the user to accept); else it decides from its pairing history.
  void begin(const uint8_t mac[6], bool pair, uint16_t code, uint32_t nowMs) {
    const uint16_t s = seq;
    *this = Remote();
    seq = s;
    memcpy(myMac, mac, 6);
    pairing = pair;
    verify = code;
    phase = Phase::Asking;
    since = nowMs;
    uint8_t p[CONNECT_LEN];
    connectRequest(REMOTE_DEVICE_ID, mac, pair ? 1 : 0, code, p);
    send(SET_GENERAL, ID_CONNECT, CMD_WAIT_RESULT, p, CONNECT_LEN, seq++);
  }
  void end() { phase = Phase::Off, qn = 0; }

  // A whole frame from the camera
  void onFrame(const Frame& f, uint32_t nowMs) {
    if (f.cmdSet == SET_GENERAL && f.cmdId == ID_CONNECT) {
      if (f.response()) {  // its answer to our request: device id, return code
        if (phase == Phase::Asking) {
          if (f.len >= 5 && f.payload[4] != 0) {
            phase = Phase::Failed;
          } else {
            phase = Phase::Waiting;
            since = nowMs;
          }
        }
        return;
      }
      // Its own request: verify_mode 2, verify_data 0 = accepted, 1 = rejected
      if ((phase == Phase::Asking || phase == Phase::Waiting) && f.len >= 29 && f.payload[26] == 2) {
        if (get16(f.payload + 27) == 0) {
          uint8_t r[CONNECT_REPLY_LEN];
          connectReply(REMOTE_DEVICE_ID, r);
          send(SET_GENERAL, ID_CONNECT, ACK_NO_RESPONSE, r, CONNECT_REPLY_LEN, f.seq);
          uint8_t sub[SUBSCRIBE_LEN];
          subscribe(sub);
          send(SET_CAMERA, ID_SUBSCRIBE, CMD_NO_RESPONSE, sub, SUBSCRIBE_LEN, seq++);
          phase = Phase::Connected;
        } else {
          phase = Phase::Rejected;
        }
      }
      return;
    }
    if (f.cmdSet == SET_CAMERA && f.cmdId == ID_STATUS_PUSH && !f.response() && phase == Phase::Connected) {
      Status s;
      if (parseStatus(f.payload, f.len, s)) {
        const bool was = st.recording();
        st = s;
        statusAt = nowMs;
        if (!was && s.recording()) startedAt = nowMs, started = true;  // for cameraStartUtc
      }
    }
  }

  // Timeouts. Call often.
  void poll(uint32_t nowMs) {
    if (phase == Phase::Asking && nowMs - since >= REPLY_WAIT_MS) phase = Phase::Waiting, since = nowMs;  // (some cameras skip the answer)
    if (phase == Phase::Waiting && nowMs - since >= CONFIRM_WAIT_MS) phase = Phase::Failed;
  }

  // 10 a second while connected
  void sendGps(const GpsData& g, int hourOffset) {
    if (phase != Phase::Connected) return;
    uint8_t p[GPS_LEN];
    gpsPush(g, hourOffset, p);
    send(SET_GENERAL, ID_GPS, CMD_NO_RESPONSE, p, GPS_LEN, seq++);
  }
  void sendRecord(bool start) {
    if (phase != Phase::Connected) return;
    uint8_t p[RECORD_LEN];
    recordControl(start, p);
    send(SET_CAMERA, ID_RECORD, CMD_RESPONSE_OR_NOT, p, RECORD_LEN, seq++);
  }

  // The frames to write, oldest first
  int pending() const { return qn; }
  const Out& out(int i) const { return q[i]; }
  void sent() { qn = 0; }

  Phase state() const { return phase; }
  bool connected() const { return phase == Phase::Connected; }
  bool pairingAsked() const { return pairing; }
  uint16_t code() const { return verify; }
  const Status& status() const { return st; }
  uint32_t statusMs() const { return statusAt; }  // when the last status came (0: none)
  // The camera started recording since the last call: when (ms)
  bool takeStarted(uint32_t& atMs) {
    if (!started) return false;
    started = false;
    atMs = startedAt;
    return true;
  }

private:
  Phase phase = Phase::Off;
  uint8_t myMac[6] = {0};
  bool pairing = false;
  uint16_t verify = 0;
  uint16_t seq = 1;
  uint32_t since = 0, statusAt = 0, startedAt = 0;
  bool started = false;
  Status st;
  Out q[4];
  int qn = 0;

  void send(uint8_t set, uint8_t id, uint8_t type, const uint8_t* p, int len, uint16_t s) {
    if (qn >= 4) return;  // the Bluetooth side is behind: drop it (GPS comes again in 0.1 s)
    q[qn].len = buildFrame(set, id, type, p, len, s, q[qn].data, sizeof(q[qn].data));
    if (q[qn].len) qn++;
  }
};

// --- recording that follows the sessions ------------------------------------------

// When the camera should be recording: in a session, the car has moved off,
// and it isn't parked in the pits (1.3's PitWatch).
class FollowSessions {
public:
  bool update(bool sessionRunning, bool parked, float speedKmh) {
    if (!sessionRunning || parked) moved = false;
    else if (speedKmh >= MOVING_KMH) moved = true;
    return moved;
  }
  static constexpr float MOVING_KMH = 10;

private:
  bool moved = false;
};

// Sends start or stop when what's wanted changes, and again if the camera's
// status hasn't followed after RECORD_RETRY_MS (RECORD_TRIES in all). A change
// made on the camera itself is left alone until the next change here.
class RecordFollow {
public:
  void reset() { *this = RecordFollow(); }  // a new connection: compare with the camera's first status
  // -1 nothing, 0 send start, 1 send stop
  int step(uint32_t nowMs, bool want, const Status& cam) {
    if (!cam.known) return -1;
    if (!have) {
      have = true;
      wanted = want;
      if (cam.recording() == want) return -1;
      return command(nowMs);
    }
    if (want != wanted) {
      wanted = want;
      tries = 0;
      return command(nowMs);
    }
    if (tries && tries < RECORD_TRIES && cam.recording() != wanted && nowMs - sentAt >= RECORD_RETRY_MS) return command(nowMs);
    if (cam.recording() == wanted) tries = RECORD_TRIES;  // done: a later change on the camera is the driver's
    return -1;
  }

private:
  bool have = false, wanted = false;
  int tries = 0;
  uint32_t sentAt = 0;
  int command(uint32_t nowMs) {
    tries++;
    sentAt = nowMs;
    return wanted ? 0 : 1;
  }
};

}  // namespace dji

#endif  // DJI_REMOTE_H
