/**
 * Track Rat - car-specific OBD data (brake pressure, steering angle...) from
 * an OBDb car profile
 *
 * The standard OBD-II requests (Obd.h) only cover the engine. Most cars
 * also answer manufacturer requests from their other modules: brake pressure
 * and steering angle from the stability control, wheel speeds, gear. The
 * community database OBDb (https://github.com/OBDb) documents them per make
 * and model. tools/obd_car.py turns a car's OBDb signal set into a small car
 * profile on the SD card (/obd/<car>.json, see CarProfiles.h); this is what
 * the firmware does with one:
 *
 *  - Command: one request ("22 DBE5": service 0x22, data identifier DBE5) to
 *    one module, by its CAN ID (11-bit, or 29-bit like Honda's 18DA28F1),
 *    the ID its reply comes from, and BMW's extended address byte.
 *  - Signal: a value inside the reply, in OBDb's format: bit position and
 *    length, signed or not, byte order, scaling, limits.
 *  - findReply(): the reply to a command in what the ELM327 adapter sent with
 *    headers on (ATH1), frames of a long reply put back together (ISO-TP).
 *  - Target: the adapter commands that point it at a command's module (and
 *    back at the engine for the standard requests), sending only what changed.
 *
 * No OBDb data is built in: profiles live on the SD card. Pure C++, tested on
 * a PC in test/test_carobd.cpp. Untested in a car.
 */

#ifndef CAR_OBD_H
#define CAR_OBD_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace carobd {

constexpr int MAX_COMMANDS = 8;          // per car profile
constexpr int MAX_SIGNALS = 8;           // logged channels per car profile
constexpr uint32_t MIN_PERIOD_MS = 50;   // asked at most 20 times a second
constexpr uint32_t MAX_PERIOD_MS = 60000;

// A value's place and scaling in a reply (OBDb's "fmt"). The bits are
// counted from the first byte after the reply's echo of the request (for
// "22 DBE5", after 62 DB E5), most significant bit first.
struct Format {
  uint16_t bix = 0, len = 8;  // first bit, number of bits
  bool sign = false;          // two's complement
  bool lsbFirst = false;      // OBDb "blsb": the bytes are least significant first (whole bytes)
  float mul = 1, div = 1, add = 0;
  bool hasMin = false, hasMax = false, hasNullMin = false, hasNullMax = false;
  float min = 0, max = 0;          // clamped to these, as OBDb does
  float nullMin = 0, nullMax = 0;  // at or beyond these: the module has no value ("not available")
};

struct Signal {
  char id[40] = "";    // OBDb's ID, "BMW_BRAKE_P"
  char name[40] = "";  // "Brake pressure": the log's channel name
  char unit[12] = "";  // as the log shows it: "bar", "deg", "km/h" (unitText)
  Format fmt;
  int8_t command = -1;  // its Command in the profile
};

struct Command {
  bool ext29 = false;    // 29-bit CAN IDs
  uint32_t tx = 0;       // the request's CAN ID: 0x6F1, 0x18DA28F1
  uint32_t rx = 0;       // the reply's, 0 = any
  int16_t extAddr = -1;  // the module's extended address byte (BMW), or -1
  bool fcm1 = false;     // the adapter sends flow control itself (OBDb "fcm1", with extended addressing)
  uint8_t req[6] = {};   // service, then the identifier: 22 DB E5
  uint8_t reqLen = 0;
  uint32_t periodMs = 1000;
  bool sameModule(const Command& o) const {
    return ext29 == o.ext29 && tx == o.tx && rx == o.rx && extAddr == o.extAddr && fcm1 == o.fcm1;
  }
};

struct Profile {
  char name[32] = "";  // "BMW 128i"
  char vin[18] = "";   // the start of the VIN it's for ("WBAUP7C5"); empty: any car
  uint16_t year = 0;
  Command cmds[MAX_COMMANDS];
  int nCmds = 0;
  Signal sigs[MAX_SIGNALS];
  int nSigs = 0;
};

// The latest value of each of a profile's signals. ...At: when it was read (ms), 0: never.
struct Values {
  float v[MAX_SIGNALS] = {};
  uint32_t at[MAX_SIGNALS] = {};
  bool known(int i, uint32_t now, uint32_t staleMs) const {
    return i >= 0 && i < MAX_SIGNALS && at[i] && (int32_t)(now - at[i]) <= (int32_t)staleMs;
  }
};

// How a command has fared, for the Engine data screen and the log at the end of a drive.
enum class Answer : uint8_t {
  NotAsked,
  Yes,         // the module answered
  NoReply,     // nothing (NO DATA, or no reply in time): asked again now and then
  Refused,     // the module said it doesn't know the request (7F): asked again now and then
  Unusable,    // the adapter refused a command it needs (an old clone without ATCEA), or 29-bit on an 11-bit car
};

inline const char* answerText(Answer a) {
  switch (a) {
    case Answer::Yes: return "answering";
    case Answer::NoReply: return "no reply";
    case Answer::Refused: return "not supported";
    case Answer::Unusable: return "adapter can't ask";
    default: return "not asked yet";
  }
}

// --- reading a profile's fields ------------------------------------------------

inline int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// A whole string of hex digits (up to 8). False for anything else.
inline bool parseHex(const char* s, uint32_t& v, int& digits) {
  v = 0;
  digits = 0;
  if (!s) return false;
  for (; s[digits]; digits++) {
    const int d = hexDigit(s[digits]);
    if (d < 0 || digits >= 8) return false;
    v = v << 4 | (uint32_t)d;
  }
  return digits > 0;
}

// The module, from OBDb's "hdr" (request ID), "rax" (reply ID), "eax"
// (extended address) and "pri" (29-bit priority byte); any but hdr may be
// null. hdr: 3 digits = 11-bit ("6F1"); 4 = Honda's short form ("DA28" =
// 18DA28F1, its reply 18DAF128); 6 or 8 = 29-bit. rax: as long as hdr's ID,
// or 2 digits on 29-bit (the replying module, "28" = 18DAF128).
inline bool setModule(Command& c, const char* hdr, const char* rax, const char* eax, const char* pri) {
  uint32_t h;
  int n;
  if (!parseHex(hdr, h, n)) return false;
  uint8_t top = 0x18;
  if (pri && pri[0]) {
    uint32_t p;
    int pn;
    if (!parseHex(pri, p, pn) || pn > 2) return false;
    top = (uint8_t)p;
  }
  if (n == 3) {
    c.ext29 = false;
    c.tx = h;
    c.rx = (h >= 0x7E0 && h <= 0x7E7) ? h + 8 : 0;  // the engine's usual pairs; others need rax
  } else if (n == 4) {
    c.ext29 = true;
    c.tx = (uint32_t)top << 24 | h << 8 | 0xF1;
    c.rx = (uint32_t)top << 24 | (h & 0xFF00) << 8 | 0xF1 << 8 | (h & 0xFF);
  } else if (n == 6 || n == 8) {
    c.ext29 = true;
    c.tx = n == 6 ? (uint32_t)top << 24 | h : h;
    c.rx = (c.tx & 0xFFFF0000) | (c.tx & 0xFF) << 8 | (c.tx >> 8 & 0xFF);  // source and target swapped
  } else {
    return false;
  }
  if (rax && rax[0]) {
    uint32_t r;
    int rn;
    if (!parseHex(rax, r, rn)) return false;
    if (!c.ext29 && rn == 3) c.rx = r;
    else if (c.ext29 && rn == 2) c.rx = (c.tx & 0xFFFF0000) | 0xF1 << 8 | r;
    else if (c.ext29 && rn == 8) c.rx = r;
    else return false;
  }
  c.extAddr = -1;
  if (eax && eax[0]) {
    uint32_t e;
    int en;
    if (!parseHex(eax, e, en) || en != 2) return false;
    c.extAddr = (int16_t)e;
  }
  return true;
}

// The request, from OBDb's "cmd": {"22": "DBE5"}
inline bool setRequest(Command& c, const char* service, const char* id) {
  uint32_t s, v;
  int sn, vn;
  if (!parseHex(service, s, sn) || sn != 2 || !parseHex(id, v, vn) || vn % 2 || vn / 2 + 1 > (int)sizeof(c.req)) return false;
  c.req[0] = (uint8_t)s;
  c.reqLen = (uint8_t)(1 + vn / 2);
  for (int i = 0; i < vn / 2; i++) c.req[1 + i] = (uint8_t)(v >> (8 * (vn / 2 - 1 - i)));
  return true;
}

// OBDb's "freq" is how often to ask, in seconds
inline uint32_t periodFromSeconds(double s) {
  if (!(s > 0)) return 1000;
  const double ms = s * 1000.0;
  return ms < MIN_PERIOD_MS ? MIN_PERIOD_MS : ms > MAX_PERIOD_MS ? MAX_PERIOD_MS : (uint32_t)(ms + 0.5);
}

// OBDb's unit names, as the log and the screen show them
inline const char* unitText(const char* u) {
  struct U { const char* obdb; const char* shown; };
  static const U UNITS[] = {
    {"bars", "bar"}, {"kilopascal", "kPa"}, {"psi", "psi"}, {"degrees", "deg"}, {"kilometersPerHour", "km/h"},
    {"milesPerHour", "mph"}, {"celsius", "C"}, {"fahrenheit", "F"}, {"percent", "%"}, {"newtonMeters", "Nm"},
    {"metersPerSecondSquared", "m/s2"}, {"gravity", "g"}, {"millimeters", "mm"}, {"volts", "V"}, {"millivolts", "mV"},
    {"amps", "A"}, {"rpm", "rpm"}, {"seconds", "s"}, {"degreesPerSecond", "deg/s"}, {"kilometers", "km"},
    {"liters", "l"}, {"hertz", "Hz"}, {"watts", "W"}, {"kilowatts", "kW"}, {"scalar", "-"}, {"offon", "-"},
    {"yesno", "-"}, {"noyes", "-"}, {"unknown", "-"},
  };
  if (!u || !u[0]) return "-";
  for (const U& x : UNITS)
    if (!strcmp(u, x.obdb)) return x.shown;
  return "-";
}

// --- reading a value -----------------------------------------------------------

// The value of f in d (the reply's data after its echo, n bytes). False if
// the reply is too short, or the module says it has no value (nullmin/nullmax).
inline bool decode(const uint8_t* d, int n, const Format& f, float& out) {
  if (f.len == 0 || f.len > 32) return false;
  uint32_t raw = 0;
  if (f.lsbFirst) {
    const int first = f.bix / 8, bytes = (f.len + 7) / 8;
    if (first + bytes > n) return false;
    for (int i = bytes - 1; i >= 0; i--) raw = raw << 8 | d[first + i];
    if (f.len < 32) raw &= (1u << f.len) - 1;
  } else {
    if ((f.bix + f.len + 7) / 8 > n) return false;
    for (int i = f.bix; i < f.bix + f.len; i++) raw = raw << 1 | ((d[i / 8] >> (7 - i % 8)) & 1);
  }
  double v = raw;
  if (f.sign && f.len < 32 && (raw & (1u << (f.len - 1)))) v = (double)raw - (double)(1ull << f.len);
  else if (f.sign && f.len == 32) v = (int32_t)raw;
  v = v * f.mul / (f.div != 0 ? f.div : 1) + f.add;
  if (f.hasNullMax && v >= f.nullMax) return false;
  if (f.hasNullMin && v <= f.nullMin) return false;
  if (f.hasMax && v > f.max) v = f.max;
  if (f.hasMin && v < f.min) v = f.min;
  out = (float)v;
  return true;
}

// --- reading a reply (headers on) ------------------------------------------------

// The reply to c in the adapter's text (up to its prompt), with headers on:
// one CAN frame a line, "629 F1 03 62 DB E5 ..." (ID, extended address if
// any, then the ISO-TP frame), spaces optional. Frames from other IDs are
// passed over, a long reply's frames put back together, and replies to
// something else (a late answer to an earlier request) skipped.
// Returns the reply's length in out (from its first byte, 0x62), 0 if there
// is none, or -1 if the module refused the request (7F).
inline int findReply(const char* s, int n, const Command& c, uint8_t* out, int cap) {
  const int idDigits = c.ext29 ? 8 : 3;
  int got = -1, want = 0;  // a long reply being put back together: bytes so far, its length
  uint8_t nextSeq = 1;
  bool refused = false;
  auto complete = [&](const uint8_t* m, int len) -> int {  // a whole reply: ours?
    if (len >= 3 && m[0] == 0x7F && m[1] == c.req[0]) {
      if (m[2] == 0x78) return 0;  // "response pending": the real one follows
      refused = true;
      return 0;
    }
    if (len < c.reqLen || m[0] != (uint8_t)(c.req[0] + 0x40)) return 0;
    for (int k = 1; k < c.reqLen; k++)
      if (m[k] != c.req[k]) return 0;
    const int keep = len < cap ? len : cap;
    if (m != out) memmove(out, m, keep);
    return keep;
  };
  int i = 0;
  while (i < n) {
    int a = i;
    while (i < n && s[i] != '\r' && s[i] != '\n') i++;
    const int b = i;
    while (i < n && (s[i] == '\r' || s[i] == '\n')) i++;
    char hex[80];
    int h = 0;
    bool ok = true;
    for (int k = a; k < b; k++) {
      if (s[k] == ' ') continue;
      if (hexDigit(s[k]) < 0 || h >= (int)sizeof(hex)) {
        ok = false;
        break;
      }
      hex[h++] = s[k];
    }
    if (!ok || h < idDigits + 2 || (h - idDigits) % 2) continue;  // not a frame (OK, NO DATA, an echo...)
    uint32_t id = 0;
    for (int k = 0; k < idDigits; k++) id = id << 4 | (uint32_t)hexDigit(hex[k]);
    if (c.rx && id != c.rx) continue;
    uint8_t f[40];
    int fl = 0;
    for (int k = idDigits; k + 1 < h && fl < (int)sizeof(f); k += 2) f[fl++] = (uint8_t)(hexDigit(hex[k]) << 4 | hexDigit(hex[k + 1]));
    int p = c.extAddr >= 0 ? 1 : 0;  // the extended address (the tester's, F1) comes first
    if (fl <= p) continue;
    const uint8_t pci = f[p];
    if ((pci >> 4) == 0) {  // a single frame
      const int len = pci & 0x0F;
      if (len == 0 || p + 1 + len > fl) continue;
      const int r = complete(f + p + 1, len);
      if (r > 0) return r;
      got = -1;
    } else if ((pci >> 4) == 1 && fl >= p + 2) {  // the first frame of a long reply
      want = (pci & 0x0F) << 8 | f[p + 1];
      got = 0;
      nextSeq = 1;
      for (int k = p + 2; k < fl && got < want && got < cap; k++) out[got++] = f[k];
    } else if ((pci >> 4) == 2 && got >= 0) {  // the next part of it
      if ((pci & 0x0F) != (nextSeq & 0x0F)) {  // one went missing: this reply is lost
        got = -1;
        continue;
      }
      nextSeq++;
      for (int k = p + 1; k < fl && got < want && got < cap; k++) out[got++] = f[k];
      if (got >= want || got >= cap) {
        const int r = complete(out, got);
        if (r > 0) return r;
        got = -1;
      }
    }
  }
  return refused ? -1 : 0;
}

// --- pointing the adapter at a module ---------------------------------------------

// The adapter settings a request needs, one per kind, as the ELM327 command
// that sets it. An empty slot: anything will do (left as it is).
struct Target {
  enum Slot { Headers, Priority, Header, RxFilter, ExtAddr, FcHeader, FcData, FcMode, SLOTS };
  char cmd[SLOTS][16] = {};
};

// What c's module needs: headers on (to tell the replies apart), its IDs,
// its extended address, and flow control by the adapter if OBDb says so.
inline Target moduleTarget(const Command& c) {
  Target t;
  snprintf(t.cmd[Target::Headers], 16, "ATH1");
  if (c.ext29) {
    snprintf(t.cmd[Target::Priority], 16, "ATCP%02X", (unsigned)(c.tx >> 24));
    snprintf(t.cmd[Target::Header], 16, "ATSH%06X", (unsigned)(c.tx & 0xFFFFFF));
  } else {
    snprintf(t.cmd[Target::Header], 16, "ATSH%03X", (unsigned)c.tx);
  }
  if (c.rx) snprintf(t.cmd[Target::RxFilter], 16, c.ext29 ? "ATCRA%08X" : "ATCRA%03X", (unsigned)c.rx);
  else snprintf(t.cmd[Target::RxFilter], 16, "ATCRA");
  if (c.extAddr >= 0) snprintf(t.cmd[Target::ExtAddr], 16, "ATCEA%02X", (unsigned)c.extAddr);
  else snprintf(t.cmd[Target::ExtAddr], 16, "ATCEA");
  if (c.fcm1) {
    if (c.ext29) snprintf(t.cmd[Target::FcHeader], 16, "ATFCSH%08X", (unsigned)c.tx);
    else snprintf(t.cmd[Target::FcHeader], 16, "ATFCSH%03X", (unsigned)c.tx);
    if (c.extAddr >= 0) snprintf(t.cmd[Target::FcData], 16, "ATFCSD%02X300000", (unsigned)c.extAddr);
    else snprintf(t.cmd[Target::FcData], 16, "ATFCSD300000");
    snprintf(t.cmd[Target::FcMode], 16, "ATFCSM1");
  } else {
    snprintf(t.cmd[Target::FcMode], 16, "ATFCSM0");
  }
  return t;
}

// Back to the standard requests (Elm327.h, headers off): the engine's
// functional ID, no filter, no extended address, the adapter's own flow control.
inline Target standardTarget(bool car29) {
  Target t;
  snprintf(t.cmd[Target::Headers], 16, "ATH0");
  if (car29) {
    snprintf(t.cmd[Target::Priority], 16, "ATCP18");
    snprintf(t.cmd[Target::Header], 16, "ATSHDB33F1");
  } else {
    snprintf(t.cmd[Target::Header], 16, "ATSH7DF");
  }
  snprintf(t.cmd[Target::RxFilter], 16, "ATCRA");
  snprintf(t.cmd[Target::ExtAddr], 16, "ATCEA");
  snprintf(t.cmd[Target::FcMode], 16, "ATFCSM0");
  return t;
}

// What the adapter is set to, as far as we know: the commands still to send
// to reach a target are the slots that differ.
struct AdapterState {
  char cmd[Target::SLOTS][16] = {};
  void forget() { *this = AdapterState(); }  // after ATZ, or a set-up of unknown effect
  // The commands to reach t, in order, into out (each up to 16 chars). Returns how many.
  int stepsTo(const Target& t, char (*out)[16], int max) const {
    int n = 0;
    for (int k = 0; k < Target::SLOTS && n < max; k++)
      if (t.cmd[k][0] && strcmp(t.cmd[k], cmd[k])) memcpy(out[n++], t.cmd[k], 16);
    return n;
  }
  // The adapter accepted c (one of the target's commands)
  void applied(const char* c) {
    static const char* const PREFIX[Target::SLOTS] = {"ATH", "ATCP", "ATSH", "ATCRA", "ATCEA", "ATFCSH", "ATFCSD", "ATFCSM"};
    int best = -1;
    size_t bestLen = 0;
    for (int k = 0; k < Target::SLOTS; k++) {  // the longest prefix: ATFCSH is not ATFCSM
      const size_t l = strlen(PREFIX[k]);
      if (!strncmp(c, PREFIX[k], l) && l > bestLen) best = k, bestLen = l;
    }
    if (best >= 0) snprintf(cmd[best], 16, "%s", c);
  }
};

// --- choosing a car ---------------------------------------------------------------

// The profile for the car with this VIN (empty if the car didn't say): the
// longest VIN start that matches, else the only profile with no VIN, else none (-1).
inline int chooseProfile(const Profile* p, int n, const char* vin) {
  int best = -1;
  size_t bestLen = 0;
  int anyCar = -1, anyCount = 0;
  for (int i = 0; i < n; i++) {
    const size_t l = strlen(p[i].vin);
    if (!l) {
      anyCar = i;
      anyCount++;
      continue;
    }
    if (vin && strlen(vin) >= l && !strncmp(vin, p[i].vin, l) && l > bestLen) best = i, bestLen = l;
  }
  if (best >= 0) return best;
  return anyCount == 1 ? anyCar : -1;
}

// The VIN from a mode 09 PID 02 reply's bytes (49 02 01 then 17 characters), into out (18 bytes)
inline bool vinFromReply(const uint8_t* d, int n, char* out) {
  for (int i = 0; i + 3 <= n; i++) {
    if (d[i] != 0x49 || d[i + 1] != 0x02) continue;
    int k = i + 2;
    if (k < n && d[k] == 0x01) k++;  // the number of data items (CAN)
    int m = 0;
    for (; k < n && m < 17; k++) {
      const char ch = (char)d[k];
      if (ch == 0) continue;  // padding before the VIN on some cars
      if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z'))) return false;
      out[m++] = ch;
    }
    out[m] = 0;
    return m == 17;
  }
  return false;
}

}  // namespace carobd

#endif  // CAR_OBD_H
