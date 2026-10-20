/**
 * Host-side tests for the ELM327 conversation of a Bluetooth LE OBD-II
 * adapter (Elm327.h, 6.1), against a simulated adapter that answers like the
 * recorded conversations of real ones: the set-up, a clone that rejects the
 * "1" suffix, SEARCHING... before the first reply, replies split across
 * Bluetooth notifications, a missing prompt, multi-PID and multi-frame replies,
 * an engine that stops answering, the pedal-only throttle, and the schedule's
 * rates at 10, 20 and 40 replies a second. No real adapter has been tried yet.
 *
 *   g++ -std=c++17 -Iinclude test/test_elm327.cpp -o /tmp/elm && /tmp/elm
 */

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "Elm327.h"
#include "check.h"

using namespace elm;

// "41 00" + the mask with a bit for each PID in pids (from base + 1)
static std::string maskReply(uint8_t base, std::vector<uint8_t> pids) {
  uint32_t m = 0;
  for (uint8_t p : pids)
    if (p > base && p <= base + 32) m |= 0x80000000u >> (p - base - 1);
  char s[32];
  snprintf(s, sizeof(s), "41%02X%08X", base, (unsigned)m);
  return s;
}

// A simulated adapter: answers each command after `latencyMs`, in chunks of `chunk` bytes.
struct Adapter {
  std::vector<uint8_t> pids = {0x05, 0x0C, 0x0F, 0x11, 0x20};  // what the car supports, 0x01..0x20
  std::vector<uint8_t> pids2 = {0x2F, 0x33};                    // 0x21..0x40
  std::vector<uint8_t> pids3;                                   // 0x41..0x60
  bool takesSuffix = true, takesMulti = true;
  bool searching = true;      // the first request finds the protocol
  bool spaces = false;        // a clone that ignores ATS0
  bool carOn = true;
  int silentNext = 0;         // the next n requests get no reply at all
  int chunk = 20;             // bytes per notification
  uint32_t latencyMs = 50;
  std::vector<std::string> log;
  int rpm = 3000 * 4, throttle = 128, coolant = 90 + 40, intake = 25 + 40;

  std::string answer(const std::string& cmd) {
    if (cmd.rfind("AT", 0) == 0) {
      if (cmd == "ATZ") return "\r\rELM327 v1.5\r\r>";
      if (cmd == "ATDP") return "AUTO, ISO 15765-4 (CAN 11/500)\r\r>";
      return "OK\r\r>";
    }
    if (!carOn) return "NO DATA\r\r>";
    std::string pre = searching ? "SEARCHING...\r" : "";
    searching = false;
    // "01" then PIDs (two hex each), then perhaps the expected-replies digit
    std::string body = cmd.substr(2);
    bool suffix = body.size() % 2 == 1;
    if (suffix) {
      if (!takesSuffix) return "?\r\r>";
      body.pop_back();
    }
    if (body.size() > 2 && !takesMulti) return "?\r\r>";
    std::string out = "41";
    for (size_t i = 0; i < body.size(); i += 2) {
      const int pid = std::stoi(body.substr(i, 2), nullptr, 16);
      char b[16];
      if (pid == 0x00) out += maskReply(0x00, pids).substr(2);
      else if (pid == 0x20) out += maskReply(0x20, pids2).substr(2);
      else if (pid == 0x40) out += maskReply(0x40, pids3).substr(2);
      else if (pid == 0x0C) snprintf(b, sizeof(b), "0C%04X", rpm), out += b;
      else if (pid == 0x11 || pid == 0x45 || pid == 0x49) snprintf(b, sizeof(b), "%02X%02X", pid, throttle), out += b;
      else if (pid == 0x05) snprintf(b, sizeof(b), "05%02X", coolant), out += b;
      else if (pid == 0x0F) snprintf(b, sizeof(b), "0F%02X", intake), out += b;
      else return pre + "NO DATA\r\r>";
    }
    if (spaces) {
      std::string sp;
      for (size_t i = 0; i < out.size(); i += 2) sp += (i ? " " : "") + out.substr(i, 2);
      out = sp;
    }
    return pre + out + "\r\r>";
  }
};

// Runs the client against the adapter for ms milliseconds, 1 ms at a time.
struct Bench {
  Client c;
  Adapter a;
  uint32_t t = 1000;
  std::string pending;
  uint32_t replyAt = 0;
  std::vector<std::string> sent;

  void connect() { c.begin(t); }
  void run(uint32_t ms) {
    const uint32_t end = t + ms;
    for (; t < end; t++) {
      if (!pending.empty() && t >= replyAt) {  // the adapter's reply, a notification at a time
        for (size_t i = 0; i < pending.size(); i += a.chunk) {
          const std::string part = pending.substr(i, a.chunk);
          c.feed(part.data(), (int)part.size(), t);
        }
        pending.clear();
      }
      char out[32];
      if (c.poll(t, out, sizeof(out))) {
        std::string cmd(out);
        CHECK(!cmd.empty() && cmd.back() == '\r', "commands end in CR");
        cmd.pop_back();
        sent.push_back(cmd);
        if (a.silentNext > 0) {
          a.silentNext--;
          continue;
        }
        pending = a.answer(cmd);
        replyAt = t + a.latencyMs;
      }
    }
  }
  int count(const std::string& cmd) const {
    int n = 0;
    for (auto& s : sent) n += s == cmd;
    return n;
  }
  int index(const std::string& cmd) const {
    for (size_t i = 0; i < sent.size(); i++)
      if (sent[i] == cmd) return (int)i;
    return -1;
  }
};

int main() {
  printf("ELM327 tests\n");

  // Reading replies
  {
    Reply r = parseReply("OK\r\r", 4);
    CHECK(r.kind == Reply::Ok, "OK");
    r = parseReply("NO DATA\r", 8);
    CHECK(r.kind == Reply::NoData, "NO DATA");
    r = parseReply("?\r", 2);
    CHECK(r.kind == Reply::Unknown, "?");
    r = parseReply("STOPPED\r", 8);
    CHECK(r.kind == Reply::Stopped, "STOPPED");
    const char* errs[] = {"CAN ERROR", "UNABLE TO CONNECT", "BUS INIT: ...ERROR", "BUS ERROR", "BUFFER FULL"};
    for (const char* e : errs) CHECK(parseReply(e, (int)strlen(e)).kind == Reply::Error, "%s is an error", e);
    const char* busInit = "BUS INIT: ...OK\r410C1AF8\r";
    r = parseReply(busInit, (int)strlen(busInit));
    CHECK(r.kind == Reply::Data && r.len == 4, "BUS INIT OK, then data");
    const char* search = "SEARCHING...\r410C1AF8\r\r";
    r = parseReply(search, (int)strlen(search));
    CHECK(r.kind == Reply::Data && r.searching && r.len == 4 && r.data[2] == 0x1A, "SEARCHING... then data");
    const char* text = "ELM327 v1.5\r";
    r = parseReply(text, (int)strlen(text));
    CHECK(r.kind == Reply::Text && !strcmp(r.text, "ELM327 v1.5"), "text: '%s'", r.text);
    const char* spaced = "41 0C 1A F8 \r";
    r = parseReply(spaced, (int)strlen(spaced));
    CHECK(r.kind == Reply::Data && r.len == 4 && r.data[3] == 0xF8, "spaces left on");
    const char* echo = "010C\r410C1AF8\r";
    r = parseReply(echo, (int)strlen(echo));
    CHECK(r.kind == Reply::Data && r.len == 4 && r.data[0] == 0x41, "an echo is not data");
    const char* neg = "7F0112\r";
    CHECK(parseReply(neg, (int)strlen(neg)).kind == Reply::NoData, "a negative reply");

    // Two engine computers answer
    obd::Values v;
    uint8_t got = 0;
    const char* two = "410C1AF8\r410C1B00\r";
    r = parseReply(two, (int)strlen(two));
    CHECK(readMode01(r, v, 100, nullptr, &got) == 2 && got == GOT_RPM && fabsf(v.rpm - 0x1B00 / 4.0f) < 0.01f, "two ECUs: the last wins");
    // Several PIDs in one reply
    const char* multi = "410C1AF8113305820F41\r";
    r = parseReply(multi, (int)strlen(multi));
    CHECK(readMode01(r, v, 200, nullptr, &got) == 4 && got == (GOT_RPM | GOT_THROTTLE | GOT_COOLANT | GOT_INTAKE), "multi-PID: %d", got);
    CHECK(fabsf(v.throttlePct - 0x33 * 100 / 255.0f) < 0.01f && fabsf(v.coolantC - (0x82 - 40)) < 0.01f &&
              fabsf(v.intakeC - (0x41 - 40)) < 0.01f && v.intakeAt == 200,
          "values %.1f %.1f %.1f", v.throttlePct, v.coolantC, v.intakeC);
    // A multi-frame reply (headers off): the length, then numbered frames with padding at the end
    const char* frames = "00A\r0:410C1AF81133\r1:05820F41000000\r";
    r = parseReply(frames, (int)strlen(frames));
    CHECK(r.kind == Reply::Data && r.len == 10, "multi-frame: %d bytes", r.len);
    CHECK(readMode01(r, v, 300, nullptr, &got) == 4 && got == 15, "multi-frame values");
    // The supported PIDs
    Supported sup;
    const std::string m = maskReply(0, {0x0C, 0x11, 0x20});
    r = parseReply(m.c_str(), (int)m.size());
    readMode01(r, v, 0, &sup, nullptr);
    CHECK(sup.has(0x0C) && sup.has(0x11) && sup.has(0x20) && !sup.has(0x05) && !sup.has(0x21), "supported mask");
    // A PID we can't step over ends the walk without crashing
    const char* odd = "4101000000000C1AF8\r";
    r = parseReply(odd, (int)strlen(odd));
    CHECK(readMode01(r, v, 0, nullptr, &got) == 0, "unknown PID");
  }

  // The whole set-up, from connecting to running
  {
    Bench b;
    b.connect();
    b.run(3000);
    const char* setup[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATSP0", "ATAT2", "ATST19", "0100", "0120", "ATDP", "010C1", "010C111"};
    bool inOrder = b.sent.size() > 12;
    for (int i = 0; i < 12 && inOrder; i++) inOrder = b.sent[i] == setup[i];
    CHECK(inOrder, "the set-up in order (%s ...)", b.sent.empty() ? "" : b.sent[0].c_str());
    CHECK(b.count("ATZ") == 0, "no ATZ when the adapter answers");
    CHECK(b.count("0140") == 0, "0140 only if 0120 says it's there");
    CHECK(b.c.running() && b.c.usesSuffix() && b.c.usesMulti(), "running with both speed-ups");
    CHECK(!strcmp(b.c.protocol(), "AUTO, ISO 15765-4 (CAN 11/500)"), "protocol '%s'", b.c.protocol());
    CHECK(b.c.supported().has(0x0C) && b.c.supported().has(0x33) && !b.c.supported().has(0x10), "supported PIDs");
    const obd::Values& v = b.c.values();
    CHECK(v.rpmKnown(b.t) && fabsf(v.rpm - 3000) < 0.01f, "rpm %.0f", v.rpm);
    CHECK(v.throttleKnown(b.t) && v.coolantKnown(b.t) && fabsf(v.coolantC - 90) < 0.01f && v.intakeKnown(b.t) &&
              fabsf(v.intakeC - 25) < 0.01f,
          "throttle, coolant, intake");
    CHECK(b.count("010C111") > 10 && b.count("01051") >= 1 && b.count("010F1") >= 1, "RPM and throttle together, temperatures alone");
  }

  // A clone that rejects the "1" suffix (and multi-PID): plain requests
  {
    Bench b;
    b.a.takesSuffix = b.a.takesMulti = false;
    b.connect();
    b.run(3000);
    CHECK(b.c.running() && !b.c.usesSuffix() && !b.c.usesMulti(), "running without the speed-ups");
    CHECK(b.count("010C") > 10 && b.count("0111") > 5 && b.count("0105") >= 1, "plain requests: %d rpm", b.count("010C"));
    CHECK(b.c.values().rpmKnown(b.t) && b.c.values().throttleKnown(b.t), "values still come");
    // Only multi rejected
    Bench m;
    m.a.takesMulti = false;
    m.connect();
    m.run(3000);
    CHECK(m.c.usesSuffix() && !m.c.usesMulti() && m.count("010C1") > 10, "suffix but no multi-PID");
  }

  // Spaces left on, and a reply cut into small notifications, and one byte at a time
  for (int chunk : {1, 7, 20}) {
    Bench b;
    b.a.spaces = true;
    b.a.chunk = chunk;
    b.connect();
    b.run(2000);
    CHECK(b.c.running() && b.c.values().rpmKnown(b.t) && fabsf(b.c.values().rpm - 3000) < 0.01f, "%d-byte notifications", chunk);
  }

  // SEARCHING... takes 4 s on the first request: still fine (it's within the search timeout)
  {
    Bench b;
    b.connect();
    b.run(400);  // the AT commands
    b.a.latencyMs = 4000;
    b.run(4500);
    b.a.latencyMs = 50;
    b.run(2000);
    CHECK(b.c.running() && b.count("0100") == 1 && b.count("ATZ") == 0, "a slow search: 0100 sent %d times", b.count("0100"));
  }

  // A missing prompt: the request times out and the next one goes; three in a row: ATZ and set up again
  {
    Bench b;
    b.connect();
    b.run(2000);
    const size_t before = b.sent.size();
    b.a.silentNext = 1;
    b.run(1500);
    CHECK(b.c.statistics().timeouts == 1 && b.c.running() && b.sent.size() > before + 3, "one missed prompt: carries on");
    b.a.silentNext = 3;
    b.run(5000);
    CHECK(b.count("ATZ") == 1, "three missed: ATZ (%d)", b.count("ATZ"));
    const int z = b.index("ATZ");
    CHECK(z >= 0 && (int)b.sent.size() > z + 1 && b.sent[z + 1] == "ATE0", "then the set-up again");
    CHECK(b.c.running(), "and running again");
  }

  // The engine stops answering (ignition off): find the car again every few seconds
  {
    Bench b;
    b.connect();
    b.run(2000);
    b.a.carOn = false;
    b.run(1000);
    CHECK(b.c.state() == Client::Phase::NoCar, "no car after NO DATA in a row");
    const int asked = b.count("0100");
    b.run(RETRY_MS + 200);
    CHECK(b.count("0100") == asked + 1, "asks again after RETRY_MS");
    b.a.carOn = true;
    b.run(3000);
    CHECK(b.c.running() && b.c.values().rpmKnown(b.t), "back when the engine is");
    // Not plugged in at all to start with
    Bench off;
    off.a.carOn = false;
    off.connect();
    off.run(2000);
    CHECK(off.c.state() == Client::Phase::NoCar && off.count("0100") == 1, "no car from the start");
  }

  // Throttle from the pedal when 0x11 isn't supported
  {
    Bench b;
    b.a.pids = {0x0C, 0x20};
    b.a.pids2 = {0x40};
    b.a.pids3 = {0x49};
    b.connect();
    b.run(3000);
    CHECK(b.count("0140") == 1 && b.count("010C491") > 5 && b.count("0111") == 0, "RPM with the pedal (0x49)");
    CHECK(b.c.values().throttleKnown(b.t) && !b.c.values().coolantKnown(b.t), "throttle known, coolant not supported");
  }

  // Each value is stamped at the middle of its request
  {
    Bench b;
    b.a.latencyMs = 80;
    b.connect();
    b.run(3000);
    const obd::Values& v = b.c.values();
    // the last reply arrived at most a request ago; its stamp is 40 ms before it arrived
    CHECK(v.rpmAt > 0 && b.t - v.rpmAt >= 40 && b.t - v.rpmAt < 80 + 40 + 5, "stamp %u ms ago", (unsigned)(b.t - v.rpmAt));
  }

  // The schedule's rates, at 10, 20 and 40 replies a second (one PID per request)
  for (int perSecond : {10, 20, 40}) {
    obd::Schedule s;
    s.setPids(obd::PID_RPM, obd::PID_THROTTLE, obd::PID_COOLANT, obd::PID_INTAKE);
    int n[256] = {0};
    const uint32_t step = 1000 / perSecond, seconds = 20;
    for (uint32_t t = 0; t < seconds * 1000; t += step) n[s.next(t)]++;
    const float rpm = n[obd::PID_RPM] / (float)seconds, thr = n[obd::PID_THROTTLE] / (float)seconds;
    const float cool = n[obd::PID_COOLANT] / (float)seconds, in = n[obd::PID_INTAKE] / (float)seconds;
    const float fast = perSecond - 2;  // what's left after the temperatures
    CHECK(fabsf(cool - 1) < 0.15f && fabsf(in - 1) < 0.15f, "%d/s: temperatures about 1 Hz (%.2f %.2f)", perSecond, cool, in);
    CHECK(fabsf(rpm - fast * 0.6f) < 0.6f && fabsf(thr - fast * 0.4f) < 0.6f, "%d/s: rpm %.1f Hz, throttle %.1f Hz", perSecond, rpm, thr);
    CHECK(rpm > thr, "RPM most often");
  }
  {
    obd::Schedule s;
    s.setPids(obd::PID_RPM, 0, 0, 0);
    CHECK(s.next(0) == obd::PID_RPM && s.next(1) == obd::PID_RPM, "RPM only");
    s.setPids(0, 0, 0, 0);
    CHECK(s.next(0) == 0, "nothing supported");
  }

  // The diagnostics' rates come from the replies
  {
    Bench b;
    b.a.latencyMs = 40;
    b.connect();
    b.run(5000);
    const Client::Stats& st = b.c.statistics();
    CHECK(st.requestHz > 15 && st.rpmHz > 10 && st.rpmHz == st.throttleHz && st.coolantHz > 0.5f && st.coolantHz < 1.5f,
          "requests %.1f/s, rpm %.1f Hz, throttle %.1f Hz, coolant %.1f Hz", st.requestHz, st.rpmHz, st.throttleHz, st.coolantHz);
  }

  // Disconnected: nothing to send
  {
    Bench b;
    b.connect();
    b.run(500);
    b.c.end();
    const size_t n = b.sent.size();
    b.run(1000);
    CHECK(b.sent.size() == n && b.c.state() == Client::Phase::Off, "silent once off");
  }

  if (failures) {
    printf("%d FAILED\n", failures);
    return 1;
  }
  printf("All ELM327 tests passed\n");
  return 0;
}
