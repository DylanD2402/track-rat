/**
 * Track Rat - the ELM327 protocol of a Bluetooth LE OBD-II adapter (6.1)
 *
 * Adapters such as the Veepeak OBDCheck BLE+ and OBDLink CX speak ELM327: text
 * commands ending in a carriage return ("010C\r"), and replies in hex ending in
 * a ">" prompt. This is the conversation, with no Bluetooth in it: ObdBle.cpp
 * passes the adapter's notifications to feed() and writes what poll() returns.
 *
 *  - Set-up: echo, line feeds, spaces and headers off, automatic protocol,
 *    aggressive adaptive timing and a short timeout. No ATZ (slow) unless
 *    recovering from an adapter that stopped answering.
 *  - Which PIDs the car supports (0100, 0120, 0140), so only those are asked.
 *  - Two speed-ups, each tried once and dropped if the adapter answers "?":
 *    the expected-replies digit ("010C1": answer as soon as one engine
 *    computer has, instead of waiting out the timeout), and RPM and throttle in
 *    one request ("010C11").
 *  - Then RPM, throttle, coolant, intake and (if the car has it) oil
 *    temperature on obd::Schedule, the values in obd::Values like the CAN path's (Obd.h). Each value is stamped
 *    at the middle of its request, the best guess at when the engine read it.
 *
 * Replies are put back together across Bluetooth notifications (20 bytes each
 * on a default connection). NO DATA, SEARCHING..., STOPPED, ?, CAN ERROR,
 * UNABLE TO CONNECT, BUS INIT and multi-line replies are all understood.
 *
 * Pure C++, tested on a PC with recorded adapter conversations in
 * test/test_elm327.cpp. Untested with a real adapter (none bought yet).
 */

#ifndef ELM327_H
#define ELM327_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "Obd.h"

namespace elm {

// --- one reply, up to the ">" prompt -----------------------------------------

struct Reply {
  enum Kind : uint8_t { Empty, Ok, Data, NoData, Unknown, Stopped, Error, Text };
  Kind kind = Empty;
  bool searching = false;  // "SEARCHING..." came first: the adapter was finding the car's protocol
  uint8_t data[64];        // the mode 01 reply bytes (0x41, PID, data...) of every line, in order
  int len = 0;
  char text[40] = "";      // the first line of a Text reply (an AT command's answer)
};

inline bool isHex(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
inline int hexVal(char c) { return c <= '9' ? c - '0' : (c & ~0x20) - 'A' + 10; }

// Does line (n chars, upper case as the adapter sends it) start with word?
inline bool startsWith(const char* line, int n, const char* word) {
  const int w = (int)strlen(word);
  return n >= w && !memcmp(line, word, w);
}

// Reads the text the adapter sent before its prompt.
inline Reply parseReply(const char* s, int n) {
  Reply r;
  bool data = false, ok = false, noData = false, unknown = false, stopped = false, error = false, text = false;
  int frameTotal = -1;  // a multi-frame reply says its length first ("00A")
  int i = 0;
  while (i < n) {
    // One line, without its ends and spaces
    int a = i;
    while (i < n && s[i] != '\r' && s[i] != '\n') i++;
    int b = i;
    while (i < n && (s[i] == '\r' || s[i] == '\n')) i++;
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == 0)) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    const char* line = s + a;
    const int len = b - a;
    if (len <= 0) continue;

    if (startsWith(line, len, "SEARCHING")) { r.searching = true; continue; }
    if (startsWith(line, len, "BUS INIT")) {  // "BUS INIT: ...OK", then the data; or "...ERROR"
      for (int k = 0; k + 5 <= len; k++)
        if (!memcmp(line + k, "ERROR", 5)) error = true;
      continue;
    }
    if (len == 2 && startsWith(line, len, "OK")) { ok = true; continue; }
    if (startsWith(line, len, "NO DATA")) { noData = true; continue; }
    if (len == 1 && line[0] == '?') { unknown = true; continue; }
    if (startsWith(line, len, "STOPPED")) { stopped = true; continue; }
    if (startsWith(line, len, "CAN ERROR") || startsWith(line, len, "UNABLE TO CONNECT") || startsWith(line, len, "BUS ERROR") ||
        startsWith(line, len, "FB ERROR") || startsWith(line, len, "DATA ERROR") || startsWith(line, len, "BUFFER FULL") ||
        startsWith(line, len, "BUS BUSY") || startsWith(line, len, "ERR") || startsWith(line, len, "LV RESET") ||
        startsWith(line, len, "ACT ALERT")) {
      error = true;
      continue;
    }

    // A frame of a multi-frame reply: "0: 41 0C ..."
    bool frame = false;
    if (len >= 2 && isHex(line[0]) && line[1] == ':') {
      frame = true;
      line += 2;
    }
    const int ll = frame ? len - 2 : len;

    // Hex, maybe with spaces (a clone that ignores ATS0)
    char hex[96];
    int h = 0;
    bool allHex = true;
    for (int k = 0; k < ll; k++) {
      if (line[k] == ' ') continue;
      if (!isHex(line[k]) || h >= (int)sizeof(hex)) {
        allHex = false;
        break;
      }
      hex[h++] = line[k];
    }
    if (allHex && !frame && h == 3) {  // the length of a multi-frame reply
      frameTotal = hexVal(hex[0]) * 256 + hexVal(hex[1]) * 16 + hexVal(hex[2]);
      continue;
    }
    if (allHex && h >= 2 && h % 2 == 0) {
      const uint8_t first = (uint8_t)(hexVal(hex[0]) * 16 + hexVal(hex[1]));
      if (frame || first == 0x41) {  // anything else (an echo of our own request) isn't the engine's
        for (int k = 0; k < h && r.len < (int)sizeof(r.data); k += 2) r.data[r.len++] = (uint8_t)(hexVal(hex[k]) * 16 + hexVal(hex[k + 1]));
        data = true;
        continue;
      }
      if (first == 0x7F) { noData = true; continue; }  // "not supported" from the engine
    }
    // Anything else: an AT command's answer ("ELM327 v1.5", "AUTO, ISO 15765-4 (CAN 11/500)")
    if (!text) {
      text = true;
      const int m = len < (int)sizeof(r.text) - 1 ? len : (int)sizeof(r.text) - 1;
      memcpy(r.text, s + a, m);
      r.text[m] = 0;
    }
  }
  if (frameTotal >= 0 && frameTotal < r.len) r.len = frameTotal;  // the last frame's padding

  r.kind = data ? Reply::Data : error ? Reply::Error : unknown ? Reply::Unknown : noData ? Reply::NoData
         : stopped ? Reply::Stopped : ok ? Reply::Ok : text ? Reply::Text : Reply::Empty;
  return r;
}

// The PIDs a car supports: bit (pid - 1) of the 0x00 mask, and so on. 0x01..0x60.
struct Supported {
  uint32_t mask[3] = {0, 0, 0};
  bool has(uint8_t pid) const {
    if (pid == 0) return true;
    if (pid > 0x60) return false;
    const int k = (pid - 1) / 32, bit = (pid - 1) % 32;
    return mask[k] & (0x80000000u >> bit);
  }
};

// Walks the mode 01 replies in r: engine values into v (stamped at), supported-PID
// masks into sup. Returns how many PIDs were read; *pids gets a bit per engine
// value: 1 RPM, 2 throttle, 4 coolant, 8 intake, 16 oil.
constexpr uint8_t GOT_RPM = 1, GOT_THROTTLE = 2, GOT_COOLANT = 4, GOT_INTAKE = 8, GOT_OIL = 16;

inline int readMode01(const Reply& r, obd::Values& v, uint32_t at, Supported* sup, uint8_t* pids) {
  int read = 0;
  if (pids) *pids = 0;
  int i = 0;
  while (i < r.len) {
    if (r.data[i] != 0x41) { i++; continue; }
    i++;
    while (i < r.len && r.data[i] != 0x41) {
      const uint8_t pid = r.data[i];
      const int n = obd::dataBytes(pid);
      if (n < 0 || i + 1 + n > r.len) { i = r.len; break; }  // one we can't step over: the rest is lost
      const uint8_t* d = r.data + i + 1;
      if (pid == 0x00 || pid == 0x20 || pid == 0x40) {
        if (sup) sup->mask[pid / 0x20] |= (uint32_t)d[0] << 24 | (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | d[3];
        read++;
      } else if (obd::decode(pid, d, v, at)) {
        read++;
        if (pids) *pids |= pid == obd::PID_RPM ? GOT_RPM : pid == obd::PID_COOLANT ? GOT_COOLANT : pid == obd::PID_INTAKE ? GOT_INTAKE
                         : pid == obd::PID_OIL ? GOT_OIL : GOT_THROTTLE;
      }
      i += 1 + n;
    }
  }
  return read;
}

// --- finding the adapter -------------------------------------------------------

// Is a Bluetooth device found by a scan worth listing under Find adapter? Its name
// (OBD, OBDII, VEEPEAK, OBDLink, vLink, IOS-Vlink, any case), or it advertises
// the FFF0 or FFE0 service that ELM327 adapters use.
inline bool looksLikeAdapter(const char* name, bool advertisesFff0OrFfe0) {
  if (advertisesFff0OrFfe0) return true;
  if (!name) return false;
  static const char* const WORDS[] = {"OBD", "VEEPEAK", "VLINK", "ELM327"};
  char up[48];
  int n = 0;
  for (; name[n] && n < (int)sizeof(up) - 1; n++) up[n] = (name[n] >= 'a' && name[n] <= 'z') ? name[n] - 32 : name[n];
  up[n] = 0;
  for (const char* w : WORDS)
    if (strstr(up, w)) return true;
  return false;
}

// --- the conversation ---------------------------------------------------------

constexpr uint32_t AT_TIMEOUT_MS = 2000;      // an AT command's answer
constexpr uint32_t RESET_TIMEOUT_MS = 3000;   // ATZ restarts the adapter
constexpr uint32_t SEARCH_TIMEOUT_MS = 8000;  // the first request: finding the car's protocol takes seconds
constexpr uint32_t DATA_TIMEOUT_MS = 1000;    // an engine value (ATST caps the adapter's own wait well below)
constexpr uint32_t RETRY_MS = 3000;           // the car didn't answer (ignition off): ask again this much later
constexpr int TIMEOUTS_BEFORE_RESET = 3;      // prompts missed in a row before ATZ
constexpr int ERRORS_BEFORE_RESTART = 5;      // bus errors / NO DATA in a row before finding the car again
constexpr int UNKNOWNS_BEFORE_PLAIN = 3;      // "?" in a row to the speed-ups before doing without them
constexpr uint32_t LATE_REPLY_MS = 200;       // after a missed prompt: wait this long before the next request

class Client {
public:
  enum class Phase : uint8_t { Off, Setup, Pids, Probe, Running, NoCar };

  // The adapter connected (or reconnected): start from the set-up.
  void begin(uint32_t nowMs) {
    *this = Client();
    phase = Phase::Setup;
    nextAt = nowMs;
    rateFrom = nowMs;
  }
  // The adapter disconnected.
  void end() { phase = Phase::Off; }

  // The adapter's bytes, as they come (any split). Completes a reply at each ">".
  void feed(const char* s, int n, uint32_t nowMs) {
    for (int k = 0; k < n; k++) {
      if (s[k] == '>') {
        reply(nowMs);
        len = 0;
      } else if (len < (int)sizeof(buf)) {
        buf[len++] = s[k];
      }
    }
  }

  // The command to send now, with its "\r", into out (at least 16 bytes). False:
  // nothing to send (waiting for a reply, or for the car). Call often.
  bool poll(uint32_t nowMs, char* out, int outLen) {
    rates(nowMs);
    if (phase == Phase::Off) return false;
    if (waiting) {
      if (nowMs - sentAt < timeout) return false;
      waiting = false;
      len = 0;
      stats.timeouts++;
      if (++missed >= TIMEOUTS_BEFORE_RESET) {  // the adapter has stopped answering: ATZ, then set it up again
        missed = 0;
        resetting = true;
        phase = Phase::Setup;
        step = 0;
      } else if (phase == Phase::Running) {
        failed(nowMs);
      } else if (phase == Phase::Probe) {  // no prompt to a speed-up: do without it
        probed(false, 0, nowMs);
      }
      // A quiet moment, so a late reply is taken as stray rather than as the next one's
      if (phase != Phase::NoCar) nextAt = nowMs + LATE_REPLY_MS;
    }
    if ((int32_t)(nowMs - nextAt) < 0) return false;
    if (phase == Phase::NoCar) phase = Phase::Pids, step = 0;

    char cmd[16] = "";
    timeout = DATA_TIMEOUT_MS;
    switch (phase) {
      case Phase::Setup:
        if (resetting) {
          snprintf(cmd, sizeof(cmd), "ATZ");
          timeout = RESET_TIMEOUT_MS;
        } else {
          snprintf(cmd, sizeof(cmd), "%s", setupCommand(step));
          timeout = AT_TIMEOUT_MS;
        }
        break;
      case Phase::Pids:
        if (step == 3) {
          snprintf(cmd, sizeof(cmd), "ATDP");  // the protocol found, for the diagnostics screen
          timeout = AT_TIMEOUT_MS;
        } else {
          snprintf(cmd, sizeof(cmd), "01%02X", step * 0x20);
          timeout = SEARCH_TIMEOUT_MS;
        }
        break;
      case Phase::Probe:
        if (step == 0) snprintf(cmd, sizeof(cmd), "01%02X1", sched.rpmPid() ? sched.rpmPid() : sched.throttlePid());
        else snprintf(cmd, sizeof(cmd), "01%02X%02X%s", sched.rpmPid(), sched.throttlePid(), suffix ? "1" : "");
        break;
      case Phase::Running: {
        const uint8_t pid = sched.next(nowMs);
        if (!pid) return false;
        if (multi && (pid == sched.rpmPid() || pid == sched.throttlePid()))
          snprintf(cmd, sizeof(cmd), "01%02X%02X%s", sched.rpmPid(), sched.throttlePid(), suffix ? "1" : "");
        else
          snprintf(cmd, sizeof(cmd), "01%02X%s", pid, suffix ? "1" : "");
        break;
      }
      default: return false;
    }
    const int n = snprintf(out, outLen, "%s\r", cmd);
    if (n <= 0 || n >= outLen) return false;
    snprintf(lastCmd, sizeof(lastCmd), "%s", cmd);
    waiting = true;
    sentAt = nowMs;
    stats.requests++;
    return true;
  }

  // --- what the screens show ---
  struct Stats {
    uint32_t requests = 0, replies = 0, timeouts = 0, errors = 0;
    float requestHz = 0, rpmHz = 0, throttleHz = 0, coolantHz = 0, intakeHz = 0, oilHz = 0;  // over the last second or two
  };
  Phase state() const { return phase; }
  bool running() const { return phase == Phase::Running; }
  const obd::Values& values() const { return vals; }
  const Stats& statistics() const { return stats; }
  const char* protocol() const { return proto; }  // "AUTO, ISO 15765-4 (CAN 11/500)", or "" until known
  const Supported& supported() const { return sup; }
  bool usesSuffix() const { return suffix; }
  bool usesMulti() const { return multi; }
  const char* lastCommand() const { return lastCmd; }

private:
  static constexpr int SETUP_STEPS = 7;
  static const char* setupCommand(int i) {
    static const char* const SETUP[SETUP_STEPS] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATSP0", "ATAT2", "ATST19"};  // ATST19: 25 x 4 ms
    return SETUP[i];
  }

  Phase phase = Phase::Off;
  int step = 0;
  bool resetting = false;
  bool waiting = false;
  uint32_t sentAt = 0, nextAt = 0, timeout = 0;
  int missed = 0;    // prompts missed in a row
  int failures = 0;  // requests in a row the car didn't answer
  int unknowns = 0;  // "?" in a row while using the speed-ups
  bool suffix = false, multi = false;
  char buf[256];
  int len = 0;
  char lastCmd[16] = "";
  char proto[40] = "";
  Supported sup;
  obd::Schedule sched;
  obd::Values vals;
  Stats stats;
  // counts since rateFrom, for the rates
  uint32_t rateFrom = 0, nReq = 0, nRpm = 0, nThr = 0, nCool = 0, nIntake = 0, nOil = 0;

  void rates(uint32_t nowMs) {
    const uint32_t dt = nowMs - rateFrom;
    if (dt < 1000) return;
    const float s = dt / 1000.0f;
    stats.requestHz = nReq / s, stats.rpmHz = nRpm / s, stats.throttleHz = nThr / s;
    stats.coolantHz = nCool / s, stats.intakeHz = nIntake / s, stats.oilHz = nOil / s;
    nReq = nRpm = nThr = nCool = nIntake = nOil = 0;
    rateFrom = nowMs;
  }

  // The car didn't answer a request, or the bus reported an error
  void failed(uint32_t nowMs) {
    stats.errors++;
    if (++failures >= ERRORS_BEFORE_RESTART) {  // engine off, or the adapter lost the car: find it again later
      failures = 0;
      phase = Phase::NoCar;
      nextAt = nowMs + RETRY_MS;
    }
  }

  void startRunning(uint32_t nowMs) {
    phase = Phase::Running;
    failures = 0;
    nextAt = nowMs;
  }

  // What the car supports, once the masks are in: the schedule, then the probes
  void pidsKnown(uint32_t nowMs) {
    const uint8_t thr = sup.has(obd::PID_THROTTLE) ? obd::PID_THROTTLE : sup.has(obd::PID_REL_THROTTLE) ? obd::PID_REL_THROTTLE
                      : sup.has(obd::PID_PEDAL_D) ? obd::PID_PEDAL_D : 0;
    sched.setPids(sup.has(obd::PID_RPM) ? obd::PID_RPM : 0, thr, sup.has(obd::PID_COOLANT) ? obd::PID_COOLANT : 0,
                  sup.has(obd::PID_INTAKE) ? obd::PID_INTAKE : 0, sup.has(obd::PID_OIL) ? obd::PID_OIL : 0);
    suffix = multi = false;
    phase = Phase::Probe;
    step = 0;
    if (!sched.rpmPid() && !sched.throttlePid()) startRunning(nowMs);  // nothing fast to try the speed-ups on
    nextAt = nowMs;
  }

  // A whole reply arrived
  void reply(uint32_t nowMs) {
    if (!waiting) return;  // a stray prompt (after a timeout, say)
    waiting = false;
    missed = 0;
    const Reply r = parseReply(buf, len);
    const uint32_t at = sentAt + (nowMs - sentAt) / 2;  // the middle of the request
    stats.replies++;
    nReq++;
    nextAt = nowMs;

    switch (phase) {
      case Phase::Setup:
        if (resetting) {
          resetting = false;  // whatever ATZ said ("ELM327 v1.5"): now set it up
          step = 0;
          return;
        }
        if (++step >= SETUP_STEPS) {  // a "?" (a clone without ATAT2, say) isn't worth stopping for
          phase = Phase::Pids;
          step = 0;
        }
        return;

      case Phase::Pids: {
        if (step == 3) {  // ATDP
          if (r.kind == Reply::Text) snprintf(proto, sizeof(proto), "%s", r.text);
          pidsKnown(nowMs);
          return;
        }
        uint8_t got = 0;
        if (r.kind == Reply::Data && readMode01(r, vals, at, &sup, &got) > 0) {
          const uint8_t nextRange = (uint8_t)((step + 1) * 0x20);  // bit 0x20 of 0x00's mask: 0x21..0x40 can be asked
          step = step < 2 && sup.has(nextRange) ? step + 1 : 3;
          return;
        }
        if (step == 0) {  // no answer to the first question: ignition off, or not plugged into a car
          stats.errors++;
          phase = Phase::NoCar;
          nextAt = nowMs + RETRY_MS;
          return;
        }
        step = 3;  // the other ranges are optional
        return;
      }

      case Phase::Probe: {
        uint8_t got = 0;
        const bool data = r.kind == Reply::Data && readMode01(r, vals, at, nullptr, &got) > 0;
        count(got);
        probed(data, got, nowMs);
        return;
      }

      case Phase::Running: {
        uint8_t got = 0;
        if (r.kind == Reply::Data && readMode01(r, vals, at, nullptr, &got) > 0) {
          failures = unknowns = 0;
          count(got);
          return;
        }
        if (r.kind == Reply::Unknown && (suffix || multi)) {  // took the probe, but not this: drop the speed-ups
          if (++unknowns >= UNKNOWNS_BEFORE_PLAIN) suffix = multi = false, unknowns = 0;
          return;
        }
        if (r.kind == Reply::Stopped) return;  // interrupted: just ask the next one
        failed(nowMs);
        return;
      }

      default: return;
    }
  }

  // The answer to a speed-up probe: data, and which values came back
  void probed(bool data, uint8_t got, uint32_t nowMs) {
    if (step == 0) {
      suffix = data;  // "?" (or anything else): the clone doesn't take the digit
      step = 1;
      if (!sched.rpmPid() || !sched.throttlePid()) startRunning(nowMs);
      return;
    }
    multi = data && (got & GOT_RPM) && (got & GOT_THROTTLE);
    startRunning(nowMs);
  }

  void count(uint8_t got) {
    if (got & GOT_RPM) nRpm++;
    if (got & GOT_THROTTLE) nThr++;
    if (got & GOT_COOLANT) nCool++;
    if (got & GOT_INTAKE) nIntake++;
    if (got & GOT_OIL) nOil++;
  }
};

}  // namespace elm

#endif  // ELM327_H
