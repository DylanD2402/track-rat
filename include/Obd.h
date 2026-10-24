/**
 * Track Rat - OBD-II over CAN (optional SN65HVD230 transceiver on IO3/IO14)
 *
 * The parts that don't touch the hardware: the standard OBD-II requests
 * (mode 01, 11-bit functional address 0x7DF), reading the engine's replies
 * (0x7E8..0x7EF), a schedule that asks for the engine speed most often (the
 * shift light wants it) with throttle and coolant temperature in between, a
 * few times a second each, and the shift light's pattern for the LED bar.
 *
 * The Bluetooth LE adapter (Elm327.h, 6.1) uses the same formulas, values
 * and shift light, with its own schedule (Schedule, below): a Bluetooth round
 * trip is slower, so coolant and intake temperature are only asked once a
 * second there.
 *
 * Untested on hardware: no CAN transceiver or adapter has been fitted yet.
 * CAN_ENABLED in Config.h is off by default, as IO14 is also a GPS pin. Pure
 * C++, tested on a PC in test/test_obd.cpp.
 */

#ifndef OBD_H
#define OBD_H

#include <math.h>
#include <stdint.h>
#include "DeltaLeds.h"

namespace obd {

constexpr uint32_t REQUEST_ID = 0x7DF;  // functional: every engine control unit listens
constexpr uint32_t REPLY_FIRST = 0x7E8, REPLY_LAST = 0x7EF;

constexpr uint8_t PID_COOLANT = 0x05;   // A - 40 = deg C
constexpr uint8_t PID_RPM = 0x0C;       // (256A + B) / 4 = rpm
constexpr uint8_t PID_INTAKE = 0x0F;    // A - 40 = deg C, intake air
constexpr uint8_t PID_THROTTLE = 0x11;  // A * 100 / 255 = %
// Some cars only report the pedal: used for the throttle when 0x11 isn't supported
constexpr uint8_t PID_REL_THROTTLE = 0x45;  // A * 100 / 255 = %
constexpr uint8_t PID_PEDAL_D = 0x49;       // A * 100 / 255 = %
constexpr uint8_t PID_OIL = 0x5C;           // A - 40 = deg C, engine oil (6.5): not every car has it

constexpr uint32_t REPLY_TIMEOUT_MS = 100;  // no answer by then: ask the next one
constexpr uint32_t MIN_GAP_MS = 40;         // between requests: about 25 a second at most
constexpr uint32_t STALE_MS = 1000;         // a value older than this is unknown
constexpr uint32_t SLOW_PERIOD_MS = 1000;   // the temperatures that are asked once a second (oil on CAN, all of them by Bluetooth)
// The temperatures are only asked for once a second (SLOW_PERIOD_MS), so a reply
// that comes back a little late would make them unknown for a fix or two (logged
// as 0) with the same limit. They change slowly: allow three requests' worth.
constexpr uint32_t SLOW_STALE_MS = 3000;

// The 8 data bytes asking for one PID (mode 01), padded with 0x55 as the standard suggests
inline void request(uint8_t pid, uint8_t out[8]) {
  out[0] = 0x02;  // two bytes follow
  out[1] = 0x01;  // mode 01: current data
  out[2] = pid;
  for (int i = 3; i < 8; i++) out[i] = 0x55;
}

// The engine's latest figures. ...At: the time each was read (ms); 0: never.
// A value stamped a little after now (the middle of a request that is
// still being answered) counts as fresh.
struct Values {
  float rpm = 0, throttlePct = 0, coolantC = 0, intakeC = 0, oilC = 0;
  uint32_t rpmAt = 0, throttleAt = 0, coolantAt = 0, intakeAt = 0, oilAt = 0;
  static bool fresh(uint32_t at, uint32_t now, uint32_t limit = STALE_MS) {
    return at && (int32_t)(now - at) <= (int32_t)limit;
  }
  bool rpmKnown(uint32_t now) const { return fresh(rpmAt, now); }
  bool throttleKnown(uint32_t now) const { return fresh(throttleAt, now); }
  bool coolantKnown(uint32_t now) const { return fresh(coolantAt, now, SLOW_STALE_MS); }
  bool intakeKnown(uint32_t now) const { return fresh(intakeAt, now, SLOW_STALE_MS); }
  bool oilKnown(uint32_t now) const { return fresh(oilAt, now, SLOW_STALE_MS); }
};

// The data bytes that follow each PID in a mode 01 reply, or -1 for one we don't read.
inline int dataBytes(uint8_t pid) {
  switch (pid) {
    case 0x00: case 0x20: case 0x40: return 4;  // which PIDs are supported
    case PID_RPM: return 2;
    case PID_COOLANT: case PID_INTAKE: case PID_THROTTLE: case PID_REL_THROTTLE: case PID_PEDAL_D: case PID_OIL: return 1;
    default: return -1;
  }
}

// One PID's value from its data bytes d (dataBytes(pid) of them) into v, read at `at`
// (0 is taken as 1). False for a PID that isn't an engine value.
inline bool decode(uint8_t pid, const uint8_t* d, Values& v, uint32_t at) {
  if (!at) at = 1;
  switch (pid) {
    case PID_RPM: v.rpm = (d[0] * 256 + d[1]) / 4.0f, v.rpmAt = at; return true;
    case PID_THROTTLE: case PID_REL_THROTTLE: case PID_PEDAL_D: v.throttlePct = d[0] * 100.0f / 255.0f, v.throttleAt = at; return true;
    case PID_COOLANT: v.coolantC = d[0] - 40.0f, v.coolantAt = at; return true;
    case PID_INTAKE: v.intakeC = d[0] - 40.0f, v.intakeAt = at; return true;
    case PID_OIL: v.oilC = d[0] - 40.0f, v.oilAt = at; return true;
    default: return false;
  }
}

// One received frame. Returns the PID it answered (and puts its value in v), or -1 if it isn't
// a mode 01 reply to one of ours. nowMs: 0 is taken as 1, so a value is never "never read".
inline int parse(uint32_t id, const uint8_t* d, uint8_t len, Values& v, uint32_t nowMs) {
  if (id < REPLY_FIRST || id > REPLY_LAST || len < 4) return -1;
  const uint8_t n = d[0];  // bytes that follow
  if (n < 3 || n > 7 || d[1] != 0x41) return -1;
  const int bytes = dataBytes(d[2]);
  if (bytes < 0 || n < 2 + bytes || len < 3 + bytes) return -1;
  return decode(d[2], d + 3, v, nowMs) ? d[2] : -1;
}

// Which PID to ask for next: RPM every other time, throttle and coolant taking turns
// between, one request at a time (the next once answered, or after REPLY_TIMEOUT_MS).
// Oil temperature is slipped in once a second, until OIL_TRIES requests of it in a row
// have had no answer (most cars don't have it; one that ever answered keeps being asked).
// A miss only counts once the engine answers something else after it, so a quiet bus
// (ignition off at start-up) doesn't give up on it.
class Poller {
public:
  static constexpr int OIL_TRIES = 3;

  // The PID to send now, or -1 (waiting for a reply, or too soon). Call often.
  int due(uint32_t nowMs) const {
    if (waiting && nowMs - sentAt < REPLY_TIMEOUT_MS) return -1;
    if (sentAny && nowMs - sentAt < MIN_GAP_MS) return -1;
    return oilTurn(nowMs) ? PID_OIL : order(step);
  }
  void sent(uint32_t nowMs) {
    if (waiting && pending == PID_OIL) oilMissed = true;  // the last oil request went unanswered
    if (oilTurn(nowMs)) {
      pending = PID_OIL;
      oilAt = nowMs;
      oilAsked = true;
    } else {
      pending = order(step);
      step = (step + 1) % 4;
    }
    sentAt = nowMs;
    sentAny = waiting = true;
    if (unanswered < 255) unanswered++;
  }
  void replied(int pid) {
    if (waiting && pid == pending) waiting = false;
    if (pid == PID_OIL) oilAnswered = true;
    else if (oilMissed && oilMisses < 255) oilMisses++;  // the engine is there, it just has no oil temperature
    oilMissed = false;
    unanswered = 0;
  }
  // Requests in a row with no reply at all: no engine listening (ignition off, wrong bit rate)
  int quietCount() const { return unanswered; }

private:
  static uint8_t order(int i) {
    static const uint8_t o[4] = {PID_RPM, PID_THROTTLE, PID_RPM, PID_COOLANT};
    return o[i];
  }
  bool oilTurn(uint32_t nowMs) const {
    return (oilAnswered || oilMisses < OIL_TRIES) && (!oilAsked || nowMs - oilAt >= SLOW_PERIOD_MS);
  }
  int step = 0;
  bool waiting = false, sentAny = false;
  int pending = -1;
  uint32_t sentAt = 0;
  int unanswered = 0;
  bool oilAsked = false, oilAnswered = false, oilMissed = false;
  uint32_t oilAt = 0;
  int oilMisses = 0;
};

// The Bluetooth adapter's schedule (Elm327.h): the next PID to ask for. RPM and
// throttle take turns, RPM three times to throttle's two; coolant, intake and oil
// temperature are slipped in once a second each. At 20 replies a second that's
// RPM about 10 Hz and throttle about 7. Asks only for what the car supports
// (0: not supported).
class Schedule {
public:
  static constexpr int SLOW = 3;  // coolant, intake, oil
  void setPids(uint8_t rpmPid, uint8_t throttlePid, uint8_t coolantPid, uint8_t intakePid, uint8_t oilPid = 0) {
    *this = Schedule();
    rpm = rpmPid, throttle = throttlePid, slow[0] = coolantPid, slow[1] = intakePid, slow[2] = oilPid;
  }
  // The PID to ask for at nowMs, or 0 if the car supports none of them.
  uint8_t next(uint32_t nowMs) {
    for (int i = 0; i < SLOW; i++) {
      if (slow[i] && (!askedAny[i] || nowMs - asked[i] >= SLOW_PERIOD_MS)) {
        askedAny[i] = true;
        asked[i] = nowMs;
        return slow[i];
      }
    }
    if (!rpm || !throttle) return rpm ? rpm : throttle ? throttle : slow[0] ? slow[0] : slow[1] ? slow[1] : slow[2];
    static const bool rpmTurn[5] = {true, false, true, false, true};
    const bool r = rpmTurn[step];
    step = (step + 1) % 5;
    return r ? rpm : throttle;
  }
  uint8_t rpmPid() const { return rpm; }
  uint8_t throttlePid() const { return throttle; }

private:
  uint8_t rpm = 0, throttle = 0, slow[SLOW] = {0, 0, 0};
  uint32_t asked[SLOW] = {0, 0, 0};
  bool askedAny[SLOW] = {false, false, false};
  int step = 0;
};

// The shift light on the LED bar: from startRpm the bar fills left to right,
// green then yellow then red; at shiftRpm and above the whole bar flashes
// blue. False below startRpm (the bar shows the delta as usual).
constexpr Rgb SHIFT_GREEN = {0, 255, 0}, SHIFT_YELLOW = {255, 200, 0}, SHIFT_RED = {255, 0, 0}, SHIFT_FLASH = {0, 80, 255};
constexpr uint32_t SHIFT_FLASH_MS = 100;  // on for half of each flash

inline bool shiftLights(float rpm, float startRpm, float shiftRpm, uint32_t nowMs, Rgb* bar, int n) {
  if (rpm < startRpm || shiftRpm <= startRpm || n <= 0) return false;
  const Rgb off = {0, 0, 0};
  if (rpm >= shiftRpm) {
    const bool on = (nowMs / (SHIFT_FLASH_MS / 2)) % 2 == 0;
    for (int i = 0; i < n; i++) bar[i] = on ? SHIFT_FLASH : off;
    return true;
  }
  const float level = (rpm - startRpm) / (shiftRpm - startRpm);  // 0..1
  const int lit = 1 + (int)(level * n);                          // the first LED lights at startRpm
  for (int i = 0; i < n; i++) {
    const float at = (float)i / n;
    bar[i] = i >= lit ? off : at < 0.5f ? SHIFT_GREEN : at < 0.8f ? SHIFT_YELLOW : SHIFT_RED;
  }
  return true;
}

}  // namespace obd

#endif  // OBD_H
