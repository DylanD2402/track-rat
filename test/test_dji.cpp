/**
 * Host-side tests for the DJI camera remote protocol (DjiRemote.h, 6.2),
 * against frames from DJI's own Osmo GPS Controller Demo: the mode switch
 * example in docs/protocol_data_segment.md, the connection requests in
 * test/connect_cmd_frame_builder/connect_cmd_frame.txt and the GPS pushes in
 * test/test_gps.c. Then the handshake, the status push, putting frames back
 * together from Bluetooth notifications, and recording that follows sessions.
 * Untested with a camera.
 *
 *   g++ -std=c++17 -Iinclude test/test_dji.cpp -o /tmp/dji && /tmp/dji
 */

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "DjiRemote.h"
#include "check.h"

using namespace dji;

static std::vector<uint8_t> hex(const char* s) {
  std::vector<uint8_t> v;
  auto val = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
  for (; s[0] && s[1]; s += 2) {
    while (*s == ' ') s++;
    if (!s[0] || !s[1]) break;
    v.push_back((uint8_t)(val(s[0]) * 16 + val(s[1])));
  }
  return v;
}

// The demo's first GPS push (test_gps.c keeps it in a 70-byte row: the frame is 66)
constexpr int GPS_FRAME_LEN = 66;
static const uint8_t GPS_FRAME[70] = {
    0xAA, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0xD2, 0xDD, 0x00, 0x17, 0x4F, 0x00, 0x35, 0x01,
    0x8A, 0xA5, 0x02, 0x00, 0xBC, 0xB2, 0xE9, 0x43, 0xA3, 0x4F, 0x75, 0x0D, 0xB9, 0x6D, 0x00, 0x00, 0xDE, 0x58,
    0x24, 0x41, 0x8D, 0x9A, 0xC5, 0x41, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x03, 0x00, 0x00, 0xE8, 0x03, 0x00, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x63, 0xB5, 0xD1, 0x31};

// A frame from the camera
static std::vector<uint8_t> cameraFrame(uint8_t set, uint8_t id, uint8_t type, const uint8_t* p, int len, uint16_t seq) {
  std::vector<uint8_t> out(OVERHEAD + len);
  out.resize(buildFrame(set, id, type, p, len, seq, out.data(), (int)out.size()));
  return out;
}

int main() {
  printf("DJI remote tests\n");

  // The CRCs and frame against the demo's mode switch example
  {
    const std::vector<uint8_t> want = hex("AA1B000100000000050057EE1D04000033FF0A01473936F4FAE1D0");
    const uint8_t payload[] = {0x00, 0x00, 0x33, 0xFF, 0x0A, 0x01, 0x47, 0x39, 0x36};
    uint8_t out[64];
    const int n = buildFrame(0x1D, 0x04, CMD_RESPONSE_OR_NOT, payload, sizeof(payload), 5, out, sizeof(out));
    CHECK(n == 27 && !memcmp(out, want.data(), 27), "mode switch frame (%d bytes)", n);
    CHECK(crc16(want.data(), 10) == 0xEE57, "CRC-16 %04X", crc16(want.data(), 10));
    CHECK(crc32(want.data(), 23) == 0xD0E1FAF4, "CRC-32 %08X", (unsigned)crc32(want.data(), 23));
    Frame f;
    CHECK(parseFrame(want.data(), 27, f) && f.cmdSet == 0x1D && f.cmdId == 0x04 && f.seq == 5 && f.len == 9 && !f.response(), "parsed back");
    std::vector<uint8_t> bad = want;
    bad[15] ^= 1;
    CHECK(!parseFrame(bad.data(), 27, f), "a flipped bit fails the CRC-32");
    bad = want;
    bad[8] ^= 1;
    CHECK(!parseFrame(bad.data(), 27, f), "and in the header, the CRC-16");
    CHECK(!parseFrame(want.data(), 26, f), "a short frame");
  }

  // The connection request against the demo's frames
  {
    const char* lines[] = {
        "AA330002000000000100982F00197856341206383456789ABC00000000000000000000000000000000FE1F00000000796C667C",
        "AA330002000000000100982F00197856341206383456789ABC00000000000000000000000000000000191A0000000033C8C7BF",
        "AA330002000000000100982F00197856341206383456789ABC00000000000000000000000000000000E10200000000048F9616"};
    const uint16_t codes[] = {0x1FFE, 0x1A19, 0x02E1};
    const uint8_t mac[6] = {0x38, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    for (int i = 0; i < 3; i++) {
      const std::vector<uint8_t> want = hex(lines[i]);
      uint8_t p[CONNECT_LEN], out[64];
      connectRequest(0x12345678, mac, 0, codes[i], p);
      const int n = buildFrame(SET_GENERAL, ID_CONNECT, CMD_WAIT_RESULT, p, CONNECT_LEN, 1, out, sizeof(out));
      CHECK(n == (int)want.size() && !memcmp(out, want.data(), n), "connect request %d", i);
    }
  }

  // The GPS push against the demo's
  {
    Frame f;
    CHECK(parseFrame(GPS_FRAME, GPS_FRAME_LEN, f) && f.cmdSet == 0 && f.cmdId == ID_GPS && f.len == GPS_LEN && f.seq == 3, "demo GPS frame parses");
    const uint8_t* p = f.payload;
    CHECK(get32(p) == 20250703 && get32(p + 4) == 173450, "date %u, time %u (17:34:50 = 09:34:50 UTC + 8)", (unsigned)get32(p), (unsigned)get32(p + 4));
    CHECK(get32(p + 32) == 1000 && get32(p + 36) == 1000 && get32(p + 40) == 10 && get32(p + 44) == 14, "accuracies and satellites");
    // Rebuilt from its own values: the same bytes
    uint8_t out[80];
    CHECK(buildFrame(0, ID_GPS, CMD_NO_RESPONSE, p, GPS_LEN, 3, out, sizeof(out)) == GPS_FRAME_LEN && !memcmp(out, GPS_FRAME, GPS_FRAME_LEN), "rebuilt identically");

    // Ours, from a fix
    GpsData g;
    g.year = 2026, g.month = 10, g.day = 20;
    g.utcMs = (14 * 3600 + 3 * 60 + 9) * 1000 + 450;
    g.lat = 45.2987654, g.lon = -76.6123456;
    g.heightM = 152.4f;
    g.speedKmh = 180, g.courseDeg = 90, g.climbMs = 0.5f;
    g.hAccM = 0.8f, g.vAccM = 1.6f, g.speedAccMs = 0.12f, g.sats = 21;
    uint8_t q[GPS_LEN];
    gpsPush(g, 8, q);
    float north, east, down;
    memcpy(&north, q + 20, 4), memcpy(&east, q + 24, 4), memcpy(&down, q + 28, 4);
    CHECK(get32(q) == 20261020 && get32(q + 4) == 220309, "date %u time %u", (unsigned)get32(q), (unsigned)get32(q + 4));
    CHECK((int32_t)get32(q + 8) == -766123456 && (int32_t)get32(q + 12) == 452987654 && (int32_t)get32(q + 16) == 152400, "position and height");
    CHECK(fabsf(north) < 0.01f && fabsf(east - 5000) < 0.1f && fabsf(down + 50) < 0.01f, "velocity %.2f %.2f %.2f cm/s", north, east, down);
    CHECK(get32(q + 32) == 1600 && get32(q + 36) == 800 && get32(q + 40) == 12 && get32(q + 44) == 21, "accuracies");
    gpsPush(g, 0, q);
    CHECK(get32(q + 4) == 140309, "no offset: UTC");
    g.utcMs = (20 * 3600) * 1000;
    gpsPush(g, 8, q);
    CHECK(get32(q + 4) == 280000, "the demo's +8 doesn't wrap (28:00:00)");
    g.hAccM = -1;
    gpsPush(g, 0, q);
    CHECK(get32(q + 36) == 99999, "accuracy not known");
  }

  // Frames put back together from notifications
  {
    Assembler a;
    std::vector<uint8_t> stream = {0x00, 0x13, 0xAA};  // rubbish, and a lone SOF
    stream.insert(stream.end(), GPS_FRAME, GPS_FRAME + GPS_FRAME_LEN);
    stream.insert(stream.end(), GPS_FRAME, GPS_FRAME + GPS_FRAME_LEN);  // two in a row
    int got = 0;
    uint8_t out[MAX_FRAME];
    Frame f;
    for (size_t i = 0; i < stream.size(); i += 20) {
      a.feed(stream.data() + i, (int)(stream.size() - i < 20 ? stream.size() - i : 20));
      while (a.next(out, f)) got += f.cmdId == ID_GPS && f.len == GPS_LEN;
    }
    CHECK(got == 2, "two frames from 20-byte notifications: %d", got);
    std::vector<uint8_t> broken(GPS_FRAME, GPS_FRAME + GPS_FRAME_LEN);
    broken[40] ^= 0xFF;
    a.feed(broken.data(), GPS_FRAME_LEN);
    a.feed(GPS_FRAME, GPS_FRAME_LEN);
    got = 0;
    while (a.next(out, f)) got++;
    CHECK(got == 1, "a damaged frame is skipped, the next one read: %d", got);
  }

  // The handshake: first pairing, accepted
  {
    Remote r;
    const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    r.begin(mac, true, 4821, 1000);
    Frame f;
    CHECK(r.pending() == 1 && parseFrame(r.out(0).data, r.out(0).len, f) && f.cmdId == ID_CONNECT && f.cmdType == CMD_WAIT_RESULT, "asks to connect");
    CHECK(f.payload[26] == 1 && get16(f.payload + 27) == 4821 && f.payload[4] == 6 && !memcmp(f.payload + 5, mac, 6), "verify mode 1, the code, our MAC");
    r.sent();
    // its answer: device id, ret 0
    uint8_t ans[9] = {0x00, 0x00, 0x33, 0xFF, 0x00};
    auto fa = cameraFrame(SET_GENERAL, ID_CONNECT, ACK_NO_RESPONSE, ans, 9, f.seq);
    Frame g;
    parseFrame(fa.data(), (int)fa.size(), g);
    r.onFrame(g, 1100);
    CHECK(r.state() == Remote::Phase::Waiting, "waits for the driver to accept on the camera");
    r.poll(1100 + 20000);
    CHECK(r.state() == Remote::Phase::Waiting, "and keeps waiting a while");
    // its own request: verify mode 2, accepted
    uint8_t req[CONNECT_LEN];
    connectRequest(0x33FF0000, mac, 2, 0, req);
    auto fr = cameraFrame(SET_GENERAL, ID_CONNECT, CMD_WAIT_RESULT, req, CONNECT_LEN, 777);
    parseFrame(fr.data(), (int)fr.size(), g);
    r.onFrame(g, 25000);
    CHECK(r.connected() && r.pending() == 2, "connected: reply and subscribe (%d)", r.pending());
    Frame a1, a2;
    parseFrame(r.out(0).data, r.out(0).len, a1);
    parseFrame(r.out(1).data, r.out(1).len, a2);
    CHECK(a1.cmdId == ID_CONNECT && a1.response() && a1.seq == 777 && a1.len == CONNECT_REPLY_LEN && a1.payload[4] == 0, "the reply has its sequence number");
    CHECK(a2.cmdSet == SET_CAMERA && a2.cmdId == ID_SUBSCRIBE && a2.payload[0] == 3 && a2.payload[1] == 20, "status at 2 Hz and on change");
    r.sent();

    // GPS and record control only now
    GpsData gd;
    r.sendGps(gd, 8);
    r.sendRecord(true);
    Frame gf, rf;
    CHECK(r.pending() == 2 && parseFrame(r.out(0).data, r.out(0).len, gf) && gf.cmdId == ID_GPS && parseFrame(r.out(1).data, r.out(1).len, rf) &&
              rf.cmdId == ID_RECORD && rf.payload[4] == 0 && get32(rf.payload) == CAMERA_DEVICE_ID && rf.seq == gf.seq + 1,
          "GPS push then record start, numbered in turn");
    r.sent();

    // Status pushes: recording starts
    uint8_t st[38] = {0};
    st[0] = 0x01, st[1] = 0x01, st[30] = 0, st[37] = 83;
    auto fs = cameraFrame(SET_CAMERA, ID_STATUS_PUSH, CMD_NO_RESPONSE, st, 38, 5);
    parseFrame(fs.data(), (int)fs.size(), g);
    r.onFrame(g, 30000);
    uint32_t at = 0;
    CHECK(r.status().known && !r.status().recording() && r.status().battery == 83 && !r.takeStarted(at), "live view, 83%%");
    st[1] = 0x03, st[5] = 2, st[30] = 2;
    fs = cameraFrame(SET_CAMERA, ID_STATUS_PUSH, CMD_NO_RESPONSE, st, 38, 6);
    parseFrame(fs.data(), (int)fs.size(), g);
    r.onFrame(g, 30500);
    CHECK(r.status().recording() && r.status().recordS == 2 && r.status().tempOver == 2, "recording, too hot");
    CHECK(r.takeStarted(at) && at == 30500 && !r.takeStarted(at), "started at 30500, once");
    r.onFrame(g, 31000);
    CHECK(!r.takeStarted(at), "still recording: not a new start");
  }

  // Rejected, no answer, and reconnecting without the pop-up
  {
    const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    Remote r;
    r.begin(mac, true, 1234, 0);
    r.sent();
    uint8_t req[CONNECT_LEN];
    connectRequest(0x33FF0000, mac, 2, 1, req);
    auto fr = cameraFrame(SET_GENERAL, ID_CONNECT, CMD_WAIT_RESULT, req, CONNECT_LEN, 9);
    Frame g;
    parseFrame(fr.data(), (int)fr.size(), g);
    r.onFrame(g, 500);
    CHECK(r.state() == Remote::Phase::Rejected && r.pending() == 0, "rejected on the camera");

    Remote q;
    q.begin(mac, false, 99, 0);
    Frame f;
    parseFrame(q.out(0).data, q.out(0).len, f);
    CHECK(f.payload[26] == 0, "reconnecting: verify mode 0");
    q.poll(REPLY_WAIT_MS);
    CHECK(q.state() == Remote::Phase::Waiting, "no answer: wait for its request anyway");
    q.poll(REPLY_WAIT_MS + CONFIRM_WAIT_MS);
    CHECK(q.state() == Remote::Phase::Failed, "then give up");
    GpsData gd;
    q.sent();
    q.sendGps(gd, 8);
    CHECK(q.pending() == 0, "no GPS unless connected");
  }

  // Advertisements
  {
    const uint8_t yes[] = {0xAA, 0x08, 0x12, 0x34, 0xFA, 0x00};
    const uint8_t no[] = {0xAA, 0x08, 0x12, 0x34, 0xFB};
    CHECK(isCamera(yes, 6) && !isCamera(no, 5) && !isCamera(yes, 4), "DJI camera adverts");
  }

  // Recording follows sessions
  {
    FollowSessions fs;
    CHECK(!fs.update(false, false, 50), "no session: no");
    CHECK(!fs.update(true, false, 3), "session, still on the grid: not yet");
    CHECK(fs.update(true, false, 25) && fs.update(true, false, 2), "moved off: yes, and stays on through slow corners");
    CHECK(!fs.update(true, true, 0), "parked in the pits: stop");
    CHECK(!fs.update(true, false, 4) && fs.update(true, false, 30), "out again: when it moves");
    CHECK(!fs.update(false, false, 30), "session over");

    RecordFollow rf;
    Status cam;
    CHECK(rf.step(0, true, cam) == -1, "nothing until the camera's status is known");
    cam.known = true;
    cam.status = 0x01;
    CHECK(rf.step(100, false, cam) == -1, "agrees: nothing");
    CHECK(rf.step(200, true, cam) == 0, "wanted: start");
    CHECK(rf.step(1000, true, cam) == -1, "waits for the status");
    CHECK(rf.step(200 + RECORD_RETRY_MS, true, cam) == 0, "not recording yet: again");
    cam.status = 0x03;
    CHECK(rf.step(9000, true, cam) == -1, "recording");
    cam.status = 0x01;  // the driver stopped it on the camera
    CHECK(rf.step(20000, true, cam) == -1, "a stop on the camera is left alone");
    CHECK(rf.step(21000, false, cam) == 1, "parked: stop (already stopped, but sent once)");
    CHECK(rf.step(30000, false, cam) == -1, "and done");
    // Gives up after RECORD_TRIES
    RecordFollow g;
    Status off;
    off.known = true, off.status = 0x01;
    int sends = 0;
    for (uint32_t t = 0; t < 60000; t += 100) sends += g.step(t, true, off) == 0;
    CHECK(sends == RECORD_TRIES, "%d tries, then stops asking", sends);
    // Connected mid-session with the camera already recording: nothing to send
    RecordFollow m;
    Status rec;
    rec.known = true, rec.status = 0x03;
    CHECK(m.step(0, true, rec) == -1, "already recording");
    // Started from the top bar (the session not moving): the sessions leave it alone until the next change
    RecordFollow tb;
    Status idle;
    idle.known = true, idle.status = 0x01;
    CHECK(tb.step(0, false, idle) == -1, "not wanted, not recording");
    tb.manual(false);
    idle.status = 0x03;  // the camera started
    CHECK(tb.step(500, false, idle) == -1, "a start from the top bar is left alone");
    CHECK(tb.step(500 + 2 * RECORD_RETRY_MS, false, idle) == -1, "and not stopped later");
    CHECK(tb.step(20000, true, idle) == 0, "drive off: start sent once (already recording)");
    // Stopped from the top bar while one of the sessions' starts was still being retried
    RecordFollow rt;
    Status st0;
    st0.known = true, st0.status = 0x01;
    rt.step(0, false, st0);
    CHECK(rt.step(100, true, st0) == 0, "drive off: start");
    rt.manual(true);  // tapped stop before the camera had followed
    CHECK(rt.step(100 + RECORD_RETRY_MS, true, st0) == -1, "no retry fights the tap");
    CHECK(rt.step(30000, false, st0) == 1, "park: stop");
  }

  if (failures) {
    printf("%d FAILED\n", failures);
    return 1;
  }
  printf("All DJI remote tests passed\n");
  return 0;
}
