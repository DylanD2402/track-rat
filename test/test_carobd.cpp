/**
 * Host-side tests for car-specific OBD data (CarObd.h) and the ELM327
 * client's turns between the standard requests and a car profile's
 * (Elm327.h): reading OBDb's module and request fields, decoding values
 * (bit positions, signs, byte order, scaling, limits, "no value"), finding a
 * reply among the frames an adapter shows with headers on (long replies,
 * late answers, other modules, refusals), the adapter settings sent on the
 * way to a module and back, choosing a car by its VIN, and whole
 * conversations with a simulated adapter for a BMW-style module (11-bit,
 * extended address) and a Honda-style one (29-bit). The replies are made up
 * for these tests; no real car or adapter has been tried.
 *
 *   g++ -std=c++17 -Iinclude test/test_carobd.cpp -o /tmp/car && /tmp/car
 */

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "CarObd.h"
#include "Elm327.h"
#include "check.h"

using namespace carobd;

static bool near(float a, float b, float tol = 1e-3f) { return fabsf(a - b) <= tol; }

static Signal makeSignal(const char* id, const char* name, const char* unit, int cmd, Format f) {
  Signal s;
  snprintf(s.id, sizeof(s.id), "%s", id);
  snprintf(s.name, sizeof(s.name), "%s", name);
  snprintf(s.unit, sizeof(s.unit), "%s", unitText(unit));
  s.command = (int8_t)cmd;
  s.fmt = f;
  return s;
}

static void testFields() {
  Command c;
  CHECK(setModule(c, "6F1", "629", "29", nullptr) && !c.ext29 && c.tx == 0x6F1 && c.rx == 0x629 && c.extAddr == 0x29,
        "BMW: 11-bit, reply 629, extended address 29");
  CHECK(setModule(c, "7E0", nullptr, nullptr, nullptr) && c.rx == 0x7E8 && c.extAddr == -1, "engine: reply 7E8");
  CHECK(setModule(c, "DA28", "28", nullptr, nullptr) && c.ext29 && c.tx == 0x18DA28F1 && c.rx == 0x18DAF128, "Honda short form");
  CHECK(setModule(c, "DA28", nullptr, nullptr, nullptr) && c.rx == 0x18DAF128, "Honda: the reply from the module itself");
  CHECK(setModule(c, "DA28", nullptr, nullptr, "14") && c.tx == 0x14DA28F1 && c.rx == 0x14DAF128, "priority byte");
  CHECK(setModule(c, "18DA30F1", nullptr, nullptr, nullptr) && c.tx == 0x18DA30F1 && c.rx == 0x18DAF130, "full 29-bit ID");
  CHECK(!setModule(c, "6F", nullptr, nullptr, nullptr) && !setModule(c, "6G1", nullptr, nullptr, nullptr) &&
            !setModule(c, "6F1", "62", nullptr, nullptr) && !setModule(c, "6F1", "629", "2", nullptr),
        "bad fields refused");

  CHECK(setRequest(c, "22", "DBE5") && c.reqLen == 3 && c.req[0] == 0x22 && c.req[1] == 0xDB && c.req[2] == 0xE5, "22 DBE5");
  CHECK(setRequest(c, "21", "04") && c.reqLen == 2 && c.req[1] == 0x04, "21 04");
  CHECK(!setRequest(c, "22", "DBE") && !setRequest(c, "2", "DBE5") && !setRequest(c, "22", "0102030405060708"), "bad requests refused");

  CHECK(periodFromSeconds(0.25) == 250 && periodFromSeconds(0.01) == MIN_PERIOD_MS && periodFromSeconds(500) == MAX_PERIOD_MS &&
            periodFromSeconds(0) == 1000,
        "periods");
  CHECK(!strcmp(unitText("bars"), "bar") && !strcmp(unitText("degrees"), "deg") && !strcmp(unitText("nonsense"), "-") &&
            !strcmp(unitText(nullptr), "-"),
        "units");
  printf("  module, request and unit fields\n");
}

static void testDecode() {
  const uint8_t d[] = {0x12, 0x34, 0xFF, 0xF6, 0x80, 0x01, 0xA5, 0x0F};
  Format f;
  float v;
  f.bix = 0, f.len = 16;
  CHECK(decode(d, 8, f, v) && v == 0x1234, "16 bits, big-endian: %g", v);
  f.bix = 16, f.len = 16, f.sign = true;
  CHECK(decode(d, 8, f, v) && v == -10, "signed: %g", v);
  f.sign = false, f.lsbFirst = true, f.bix = 0;
  CHECK(decode(d, 8, f, v) && v == 0x3412, "little-endian: %g", v);
  f = Format();
  f.bix = 48, f.len = 4;
  CHECK(decode(d, 8, f, v) && v == 0xA, "a nibble: %g", v);
  f.bix = 63, f.len = 1;
  CHECK(decode(d, 8, f, v) && v == 1, "the last bit");
  f.bix = 56, f.len = 1;
  CHECK(decode(d, 8, f, v) && v == 0, "bit 56 is the top bit of byte 7");
  f = Format();
  f.bix = 32, f.len = 16, f.div = 10, f.add = -25;  // 0x8001 = 32769
  CHECK(decode(d, 8, f, v) && near(v, 3251.9f, 0.01f), "scaled: %g", v);
  f.hasMax = true, f.max = 120;
  CHECK(decode(d, 8, f, v) && v == 120, "clamped to max");
  f = Format();
  f.len = 8, f.bix = 16, f.mul = 100, f.add = -1000, f.hasNullMax = true, f.nullMax = 20000;
  CHECK(!decode(d, 8, f, v), "at nullmax: no value");
  f.hasNullMax = false, f.hasNullMin = true, f.nullMin = -1000;
  CHECK(decode(d, 8, f, v) && v == 24500, "not at nullmin: %g", v);
  f.bix = 56, f.len = 16;
  CHECK(!decode(d, 8, f, v), "past the end: no value");
  printf("  decoding: bits, signs, byte order, scaling, limits\n");
}

static Command bmwCommand() {
  Command c;
  setModule(c, "6F1", "629", "29", nullptr);
  setRequest(c, "22", "DBE5");
  c.fcm1 = true;
  return c;
}

static Command hondaCommand() {
  Command c;
  setModule(c, "DA28", "28", nullptr, nullptr);
  setRequest(c, "22", "4005");
  return c;
}

static void testFindReply() {
  uint8_t out[64];
  const Command b = bmwCommand();
  const char* single = "629F10562DBE501F4\r";
  int n = findReply(single, (int)strlen(single), b, out, sizeof(out));
  CHECK(n == 5 && out[0] == 0x62 && out[3] == 0x01 && out[4] == 0xF4, "single frame with an extended address: %d", n);
  const char* spaced = "629 F1 05 62 DB E5 01 F4\r\r";
  CHECK(findReply(spaced, (int)strlen(spaced), b, out, sizeof(out)) == 5, "with spaces");
  // A late answer to an earlier request, another module, then ours
  const char* mixed = "629F10562DBE40000\r612F10562400B0102\r629F10562DBE50064\r";
  n = findReply(mixed, (int)strlen(mixed), b, out, sizeof(out));
  CHECK(n == 5 && out[4] == 0x64, "the right reply among others: %d %02X", n, out[4]);
  // A long reply over three frames
  const char* longer = "629F1100B62DBE5010203\r629F121040506070809\r629F1220A0B\r";
  n = findReply(longer, (int)strlen(longer), b, out, sizeof(out));
  CHECK(n == 11 && out[3] == 1 && out[10] == 0x08, "three frames put together: %d", n);
  const char* gap = "629F1100B62DBE5010203\r629F122040506070809\r";
  CHECK(findReply(gap, (int)strlen(gap), b, out, sizeof(out)) == 0, "a missing frame loses the reply");
  const char* refused = "629F1037F2231\r";
  CHECK(findReply(refused, (int)strlen(refused), b, out, sizeof(out)) == -1, "refused (7F)");
  const char* pending = "629F1037F2278\r629F10562DBE50001\r";
  CHECK(findReply(pending, (int)strlen(pending), b, out, sizeof(out)) == 5, "response pending, then the reply");
  const char* none = "NO DATA\r";
  CHECK(findReply(none, (int)strlen(none), b, out, sizeof(out)) == 0, "NO DATA");
  const char* echo = "22DBE5\r629F10562DBE50001\r";
  CHECK(findReply(echo, (int)strlen(echo), b, out, sizeof(out)) == 5, "an echo is skipped");

  const Command h = hondaCommand();
  const char* honda = "18DAF128100A62400500\r18DAF12821C80102030405\r";
  n = findReply(honda, (int)strlen(honda), h, out, sizeof(out));
  CHECK(n == 10 && out[0] == 0x62 && out[3] == 0x00 && out[4] == 0xC8, "29-bit long reply: %d", n);
  const char* other = "18DAF1110462400500\r";
  CHECK(findReply(other, (int)strlen(other), h, out, sizeof(out)) == 0, "another module's reply isn't ours");
  printf("  replies with headers on: frames, late answers, other modules, refusals\n");
}

static void testTargets() {
  const Command b = bmwCommand();
  const Target t = moduleTarget(b);
  CHECK(!strcmp(t.cmd[Target::Headers], "ATH1") && !strcmp(t.cmd[Target::Header], "ATSH6F1") &&
            !strcmp(t.cmd[Target::RxFilter], "ATCRA629") && !strcmp(t.cmd[Target::ExtAddr], "ATCEA29") &&
            !strcmp(t.cmd[Target::FcHeader], "ATFCSH6F1") && !strcmp(t.cmd[Target::FcData], "ATFCSD29300000") &&
            !strcmp(t.cmd[Target::FcMode], "ATFCSM1") && !t.cmd[Target::Priority][0],
        "BMW module settings");
  const Target h = moduleTarget(hondaCommand());
  CHECK(!strcmp(h.cmd[Target::Priority], "ATCP18") && !strcmp(h.cmd[Target::Header], "ATSHDA28F1") &&
            !strcmp(h.cmd[Target::RxFilter], "ATCRA18DAF128") && !strcmp(h.cmd[Target::ExtAddr], "ATCEA") &&
            !strcmp(h.cmd[Target::FcMode], "ATFCSM0"),
        "Honda module settings");

  AdapterState a;
  const Target std11 = standardTarget(false);
  for (int k = 0; k < Target::SLOTS; k++)
    if (std11.cmd[k][0]) a.applied(std11.cmd[k]);
  char steps[Target::SLOTS][16];
  CHECK(a.stepsTo(std11, steps, Target::SLOTS) == 0, "already there");
  int n = a.stepsTo(t, steps, Target::SLOTS);
  CHECK(n == 7, "to the BMW module: %d settings", n);
  for (int k = 0; k < n; k++) a.applied(steps[k]);
  n = a.stepsTo(std11, steps, Target::SLOTS);
  CHECK(n == 5 && !strcmp(steps[0], "ATH0") && !strcmp(steps[1], "ATSH7DF") && !strcmp(steps[4], "ATFCSM0"), "back: %d", n);
  for (int k = 0; k < n; k++) a.applied(steps[k]);
  n = a.stepsTo(t, steps, Target::SLOTS);
  CHECK(n == 5, "to the module again: the flow control ID and data are still set (%d)", n);
  a.applied("ATFCSH7E0");
  CHECK(!strcmp(a.cmd[Target::FcHeader], "ATFCSH7E0") && strcmp(a.cmd[Target::FcMode], "ATFCSH7E0"), "ATFCSH isn't ATFCSM");
  printf("  adapter settings: only what changes is sent\n");
}

static void testChoose() {
  Profile p[3];
  snprintf(p[0].vin, sizeof(p[0].vin), "WBAUP");
  snprintf(p[1].vin, sizeof(p[1].vin), "2HGFG");
  CHECK(chooseProfile(p, 3, "WBAUP7C51AVJ12345") == 0, "BMW by its VIN");
  CHECK(chooseProfile(p, 3, "2HGFG21508H700001") == 1, "Civic by its VIN");
  CHECK(chooseProfile(p, 3, "JHMCM56557C404453") == 2, "another car: the one profile with no VIN");
  CHECK(chooseProfile(p, 3, "") == 2, "no VIN: the one with no VIN");
  CHECK(chooseProfile(p, 2, "") == -1, "no VIN and every profile has one: none");
  snprintf(p[2].vin, sizeof(p[2].vin), "WBAUP7C5");
  CHECK(chooseProfile(p, 3, "WBAUP7C51AVJ12345") == 2, "the longest match");

  const uint8_t vinBytes[] = {0x49, 0x02, 0x01, 'W', 'B', 'A', 'U', 'P', '7', 'C', '5', '1', 'A', 'V', 'J', '1', '2', '3', '4', '5'};
  char vin[18];
  CHECK(vinFromReply(vinBytes, sizeof(vinBytes), vin) && !strcmp(vin, "WBAUP7C51AVJ12345"), "VIN: %s", vin);
  CHECK(!vinFromReply(vinBytes, 12, vin), "a VIN cut short");
  printf("  choosing the car's profile by its VIN\n");
}

// --- conversations ------------------------------------------------------------

// A simulated adapter that keeps its settings like an ELM327: the car's
// modules answer only when it is set up for them (headers, IDs, filter,
// extended address), and the standard requests only when it isn't.
struct CarAdapter {
  bool car29 = false;
  bool takesCea = true;       // an old clone without ATCEA answers "?"
  bool vin = true;
  bool carOn = true;          // false: the ignition is off (only the adapter answers)
  uint32_t latencyMs = 30;
  // settings
  bool headers = false;
  std::string sh, cra, cea, fcsm = "0", cp = "18";
  std::vector<std::string> log;
  int brake = 0x0123;          // BMW-style DBE5: brake pressure x 10
  int hondaBrake = 0x0C8;      // Honda-style 4005: + 25 at byte 7..8
  int rpm = 3000 * 4;
  int asksBmw = 0, asksHonda = 0, misdirected = 0;

  void defaults() { headers = false, sh = cra = cea = "", fcsm = "0", cp = "18"; }
  bool standardSet() const { return !headers && (sh.empty() || sh == "7DF" || sh == "DB33F1") && cra.empty() && cea.empty(); }

  std::string answer(const std::string& cmd) {
    log.push_back(cmd);
    if (cmd == "ATZ" || cmd == "ATD") {
      defaults();
      return cmd == "ATZ" ? "\r\rELM327 v1.5\r\r>" : "OK\r\r>";
    }
    if (cmd == "ATDP") return car29 ? "AUTO, ISO 15765-4 (CAN 29/500)\r\r>" : "AUTO, ISO 15765-4 (CAN 11/500)\r\r>";
    if (cmd.rfind("ATH", 0) == 0) return headers = cmd == "ATH1", "OK\r\r>";
    if (cmd.rfind("ATSH", 0) == 0) return sh = cmd.substr(4), "OK\r\r>";
    if (cmd.rfind("ATCRA", 0) == 0) return cra = cmd.substr(5), "OK\r\r>";
    if (cmd.rfind("ATCEA", 0) == 0) {
      if (!takesCea) return "?\r\r>";
      return cea = cmd.substr(5), "OK\r\r>";
    }
    if (cmd.rfind("ATFCSM", 0) == 0) return fcsm = cmd.substr(6), "OK\r\r>";
    if (cmd.rfind("ATCP", 0) == 0) return cp = cmd.substr(4), "OK\r\r>";
    if (cmd.rfind("AT", 0) == 0) return "OK\r\r>";
    if (!carOn) return "NO DATA\r\r>";
    if (cmd == "0902") {
      if (!vin) return "NO DATA\r\r>";
      return car29 ? "014\r0:490201324847\r1:46473231353038\r2:48373030303031\r\r>"   // 2HGFG21508H700001
                   : "014\r0:490201574241\r1:55503743353141\r2:564A3132333435\r\r>";  // WBAUP7C51AVJ12345
    }
    if (cmd.rfind("01", 0) == 0) {
      if (!standardSet()) {
        misdirected++;
        return "NO DATA\r\r>";
      }
      std::string body = cmd.substr(2);
      if (body.size() % 2) body.pop_back();
      if (body == "00") return "4100BE3EA813\r\r>";  // includes 05, 0C, 0F, 11
      if (body == "20") return "NO DATA\r\r>";
      std::string out = "41";
      char b[16];
      for (size_t i = 0; i < body.size(); i += 2) {
        const int pid = std::stoi(body.substr(i, 2), nullptr, 16);
        if (pid == 0x0C) snprintf(b, sizeof(b), "0C%04X", rpm), out += b;
        else if (pid == 0x11) out += "1180";
        else if (pid == 0x05) out += "0582";
        else if (pid == 0x0F) out += "0F41";
        else return "NO DATA\r\r>";
      }
      return out + "\r\r>";
    }
    if (cmd == "22DBE5" && !car29) {
      if (headers && sh == "6F1" && cra == "629" && cea == "29") {
        asksBmw++;
        char b[64];
        snprintf(b, sizeof(b), "629F10562DBE5%04X\r\r>", brake);
        return b;
      }
      misdirected++;
      return "NO DATA\r\r>";
    }
    if (cmd == "22DBE6" && !car29) return headers && sh == "6F1" ? "629F1037F2231\r\r>" : "NO DATA\r\r>";
    if (cmd == "224005" && car29) {
      if (headers && sh == "DA28F1" && cra == "18DAF128" && cp == "18" && cea.empty()) {
        asksHonda++;
        char b[96];
        snprintf(b, sizeof(b), "18DAF128100A62400500\r18DAF128210000%04X0102\r\r>", hondaBrake);
        return b;
      }
      misdirected++;
      return "NO DATA\r\r>";
    }
    return "NO DATA\r\r>";
  }
};

struct Bench {
  elm::Client c;
  CarAdapter a;
  uint32_t t = 1000;
  std::string pending;
  uint32_t replyAt = 0;
  void run(uint32_t ms) {
    const uint32_t end = t + ms;
    for (; t < end; t++) {
      if (!pending.empty() && t >= replyAt) {
        for (size_t i = 0; i < pending.size(); i += 20) {
          const std::string part = pending.substr(i, 20);
          c.feed(part.data(), (int)part.size(), t);
        }
        pending.clear();
      }
      char out[32];
      if (c.poll(t, out, sizeof(out))) {
        std::string cmd(out);
        CHECK(!cmd.empty() && cmd.back() == '\r', "commands end in CR");
        cmd.pop_back();
        CHECK(cmd.size() < 24, "command fits: %s", cmd.c_str());
        pending = a.answer(cmd);
        replyAt = t + a.latencyMs;
      }
    }
  }
  int count(const std::string& cmd) const {
    int n = 0;
    for (auto& s : a.log) n += s == cmd;
    return n;
  }
};

static Profile bmwProfile() {
  Profile p;
  snprintf(p.name, sizeof(p.name), "BMW 128i");
  snprintf(p.vin, sizeof(p.vin), "WBAUP");
  p.cmds[0] = bmwCommand();
  p.cmds[0].periodMs = 100;
  p.cmds[1] = bmwCommand();
  setRequest(p.cmds[1], "22", "DBE6");  // a request this module refuses
  p.cmds[1].periodMs = 500;
  p.nCmds = 2;
  Format f;
  f.len = 16, f.sign = true, f.div = 10;
  p.sigs[0] = makeSignal("BMW_BRAKE_P", "Brake pressure", "bars", 0, f);
  p.sigs[1] = makeSignal("TEST_REFUSED", "Something refused", "scalar", 1, f);
  p.nSigs = 2;
  return p;
}

static void testBmwConversation() {
  Bench b;
  const Profile p = bmwProfile();
  b.c.setWantVin(true);
  b.c.begin(b.t);
  b.run(3000);
  CHECK(!b.a.log.empty() && b.a.log[0] == "ATD", "ATD first with car profiles: %s", b.a.log.empty() ? "" : b.a.log[0].c_str());
  CHECK(b.c.vinDone() && !strcmp(b.c.vin(), "WBAUP7C51AVJ12345"), "VIN: '%s'", b.c.vin());
  CHECK(chooseProfile(&p, 1, b.c.vin()) == 0, "the BMW profile");
  b.c.setProfile(&p);
  b.a.misdirected = 0;
  const uint32_t from = b.t;
  b.run(10000);
  const obd::Values& v = b.c.values();
  CHECK(b.a.misdirected == 0, "every request went to the right place (%d didn't)", b.a.misdirected);
  CHECK(v.rpmKnown(b.t) && near(v.rpm, 3000), "RPM still read: %g", v.rpm);
  CHECK(b.c.carValues().known(0, b.t, 1000) && near(b.c.carValues().v[0], 0x123 / 10.0f), "brake pressure %g",
        b.c.carValues().v[0]);
  CHECK(b.c.carAnswer(0) == Answer::Yes && b.c.carAnswer(1) == Answer::Refused, "answers: %d %d", (int)b.c.carAnswer(0),
        (int)b.c.carAnswer(1));
  const float brakeHz = b.a.asksBmw / ((b.t - from) / 1000.0f);
  const int stdReqs = b.count("010C110") + b.count("010C11") + b.count("010C") + b.count("010C1") + b.count("010C111");
  CHECK(brakeHz >= 6, "brake pressure %.1f a second", brakeHz);
  CHECK(b.count("22DBE6") <= 4, "refused: asked again only every 10 s (%d)", b.count("22DBE6"));
  printf("  BMW-style module (11-bit, extended address): brake %.1f Hz, RPM requests %d in 10 s, %.1f requests/s\n", brakeHz,
         stdReqs, b.c.statistics().requestHz);

  // A value that changes is followed
  b.a.brake = 0xFFF6;  // -1.0 bar (signed)
  b.run(1000);
  CHECK(near(b.c.carValues().v[0], -1.0f), "signed brake pressure %g", b.c.carValues().v[0]);

  // The car stops answering (ignition off) while the adapter is pointed at the module: the
  // set-up starts again from ATD, and the standard requests come back
  b.c.setProfile(nullptr);
  b.run(2000);
  CHECK(b.a.standardSet(), "back to the standard settings when the profile goes");
  if (!b.a.standardSet())
    for (size_t i = b.a.log.size() > 14 ? b.a.log.size() - 14 : 0; i < b.a.log.size(); i++) printf("    sent %s\n", b.a.log[i].c_str());
}

// The ignition goes off while the adapter is pointed at the module, then on again
static void testIgnitionOff() {
  Bench b;
  const Profile p = bmwProfile();
  b.c.setWantVin(true);
  b.c.begin(b.t);
  b.run(3000);
  b.c.setProfile(&p);
  b.run(2000);
  const int atd = b.count("ATD");
  b.a.carOn = false;
  b.run(8000);
  CHECK(b.count("ATD") > atd, "the set-up starts again from ATD (%d)", b.count("ATD") - atd);
  b.a.carOn = true;
  b.a.misdirected = 0;
  b.run(15000);
  CHECK(b.c.state() == elm::Client::Phase::Running && b.c.values().rpmKnown(b.t), "running again");
  CHECK(b.c.carValues().known(0, b.t, 1000) && b.a.misdirected == 0, "brake pressure again, nothing misdirected (%d)",
        b.a.misdirected);
  printf("  ignition off while at the module, then on: set up again, reading again\n");
}

static void testOldClone() {
  Bench b;
  b.a.takesCea = false;  // no extended addressing
  const Profile p = bmwProfile();
  b.c.setWantVin(true);
  b.c.begin(b.t);
  b.run(3000);
  b.c.setProfile(&p);
  b.a.misdirected = 0;
  b.run(5000);
  CHECK(b.c.carAnswer(0) == Answer::Unusable, "an adapter without ATCEA can't ask the BMW module (%d)", (int)b.c.carAnswer(0));
  CHECK(b.c.values().rpmKnown(b.t) && b.a.misdirected == 0, "standard requests carry on (%d misdirected)", b.a.misdirected);
  CHECK(b.count("ATCEA29") <= 1, "not tried again and again (%d)", b.count("ATCEA29"));
  printf("  an adapter without extended addressing: the module is left out, the engine still read\n");
}

static void testHondaConversation() {
  Bench b;
  b.a.car29 = true;
  Profile p;
  snprintf(p.name, sizeof(p.name), "Civic Si");
  snprintf(p.vin, sizeof(p.vin), "2HGFG");
  p.cmds[0] = hondaCommand();
  p.cmds[0].periodMs = 100;
  p.cmds[1] = bmwCommand();  // an 11-bit command in a 29-bit car's profile: skipped
  p.nCmds = 2;
  Format f;
  f.bix = 24, f.len = 16, f.add = -25, f.hasMax = true, f.max = 250;  // made up: bytes 3-4 of the data
  p.sigs[0] = makeSignal("HONDA_BRK_P", "Brake pressure", "bars", 0, f);
  p.nSigs = 1;
  b.c.setWantVin(true);
  b.c.begin(b.t);
  b.run(3000);
  CHECK(b.c.car29() && !strcmp(b.c.vin(), "2HGFG21508H700001"), "29-bit car, VIN '%s'", b.c.vin());
  b.c.setProfile(&p);
  b.a.misdirected = 0;
  b.run(5000);
  CHECK(b.a.misdirected == 0, "no misdirected requests (%d)", b.a.misdirected);
  CHECK(b.c.carAnswer(1) == Answer::Unusable, "the 11-bit command is skipped on a 29-bit car");
  CHECK(b.c.carValues().known(0, b.t, 1000) && near(b.c.carValues().v[0], 0xC8 - 25), "Honda brake %g", b.c.carValues().v[0]);
  CHECK(b.c.values().rpmKnown(b.t), "RPM on the 29-bit car");
  printf("  Honda-style module (29-bit): brake %.1f Hz\n", b.c.carHz(0));
}

static void testNoProfile() {
  // Without car profiles the conversation is exactly the old one: no ATD, no VIN
  Bench b;
  b.c.begin(b.t);
  b.run(3000);
  CHECK(b.count("ATD") == 0 && b.count("0902") == 0, "no ATD or VIN without profiles");
  CHECK(b.c.values().rpmKnown(b.t), "RPM read");
  // No VIN from the car: still runs, and the profile with no VIN fits
  Bench n;
  n.a.vin = false;
  n.c.setWantVin(true);
  n.c.begin(n.t);
  n.run(4000);
  CHECK(n.c.vinDone() && !n.c.vin()[0] && n.c.state() == elm::Client::Phase::Running, "no VIN: running without one");
  printf("  no profiles: the standard conversation; no VIN: carries on\n");
}

int main() {
  printf("Car OBD tests\n");
  testFields();
  testDecode();
  testFindReply();
  testTargets();
  testChoose();
  testBmwConversation();
  testIgnitionOff();
  testOldClone();
  testHondaConversation();
  testNoProfile();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
