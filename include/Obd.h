/**
 * Track Rat - OBD-II over CAN (optional SN65HVD230 transceiver on IO3/IO14)
 *
 * The parts that don't touch the hardware: the standard OBD-II requests
 * (mode 01, 11-bit functional address 0x7DF), reading the engine's replies
 * (0x7E8..0x7EF), a schedule that asks for the engine speed most often (the
 * shift light wants it) with throttle and coolant temperature in between, a
 * few times a second each, and the shift light's pattern for the LED bar.
 *
 * Untested on hardware: no CAN transceiver has been fitted yet. CAN_ENABLED
 * in Config.h is off by default, as IO14 is also a GPS pin. Pure C++, tested
 * on a PC in test/test_obd.cpp.
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
constexpr uint8_t PID_THROTTLE = 0x11;  // A * 100 / 255 = %

constexpr uint32_t REPLY_TIMEOUT_MS = 100;  // no answer by then: ask the next one
constexpr uint32_t MIN_GAP_MS = 40;         // between requests: about 25 a second at most
constexpr uint32_t STALE_MS = 1000;         // a value older than this is unknown

// The 8 data bytes asking for one PID (mode 01), padded with 0x55 as the standard suggests
inline void request(uint8_t pid, uint8_t out[8]) {
  out[0] = 0x02;  // two bytes follow
  out[1] = 0x01;  // mode 01: current data
  out[2] = pid;
  for (int i = 3; i < 8; i++) out[i] = 0x55;
}

// The engine's latest figures. ...At: the time each was read (ms); 0: never.
struct Values {
  float rpm = 0, throttlePct = 0, coolantC = 0;
  uint32_t rpmAt = 0, throttleAt = 0, coolantAt = 0;
  bool rpmKnown(uint32_t now) const { return rpmAt && now - rpmAt <= STALE_MS; }
  bool throttleKnown(uint32_t now) const { return throttleAt && now - throttleAt <= STALE_MS; }
  bool coolantKnown(uint32_t now) const { return coolantAt && now - coolantAt <= STALE_MS; }
};

// One received frame. Returns the PID it answered (and puts its value in v), or -1 if it isn't
// a mode 01 reply to one of ours. nowMs: 0 is taken as 1, so a value is never "never read".
inline int parse(uint32_t id, const uint8_t* d, uint8_t len, Values& v, uint32_t nowMs) {
  if (id < REPLY_FIRST || id > REPLY_LAST || len < 4) return -1;
  const uint8_t n = d[0];  // bytes that follow
  if (n < 3 || n > 7 || d[1] != 0x41) return -1;
  const uint32_t at = nowMs ? nowMs : 1;
  switch (d[2]) {
    case PID_RPM:
      if (n < 4 || len < 5) return -1;
      v.rpm = (d[3] * 256 + d[4]) / 4.0f;
      v.rpmAt = at;
      return PID_RPM;
    case PID_THROTTLE:
      v.throttlePct = d[3] * 100.0f / 255.0f;
      v.throttleAt = at;
      return PID_THROTTLE;
    case PID_COOLANT:
      v.coolantC = d[3] - 40.0f;
      v.coolantAt = at;
      return PID_COOLANT;
    default:
      return -1;
  }
}

// Which PID to ask for next: RPM every other time, throttle and coolant taking turns
// between, one request at a time (the next once answered, or after REPLY_TIMEOUT_MS).
class Poller {
public:
  // The PID to send now, or -1 (waiting for a reply, or too soon). Call often.
  int due(uint32_t nowMs) const {
    if (waiting && nowMs - sentAt < REPLY_TIMEOUT_MS) return -1;
    if (sentAny && nowMs - sentAt < MIN_GAP_MS) return -1;
    return order(step);
  }
  void sent(uint32_t nowMs) {
    sentAt = nowMs;
    sentAny = waiting = true;
    pending = order(step);
    step = (step + 1) % 4;
    if (unanswered < 255) unanswered++;
  }
  void replied(int pid) {
    if (waiting && pid == pending) waiting = false;
    unanswered = 0;
  }
  // Requests in a row with no reply at all: no engine listening (ignition off, wrong bit rate)
  int quietCount() const { return unanswered; }

private:
  static uint8_t order(int i) {
    static const uint8_t o[4] = {PID_RPM, PID_THROTTLE, PID_RPM, PID_COOLANT};
    return o[i];
  }
  int step = 0;
  bool waiting = false, sentAny = false;
  int pending = -1;
  uint32_t sentAt = 0;
  int unanswered = 0;
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
