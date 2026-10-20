/**
 * Host-side tests for OBD-II over CAN (Obd.h): the requests, reading the
 * engine's replies (and ignoring everything else on the bus), the polling
 * schedule, values going stale, and the shift light. The hardware itself is
 * untested: no CAN transceiver has been fitted yet.
 *
 *   g++ -std=c++17 -Iinclude test/test_obd.cpp -o /tmp/obd && /tmp/obd
 */

#include <cmath>
#include <cstdio>
#include "Obd.h"
#include "check.h"

using namespace obd;

int main() {
  printf("OBD-II tests\n");

  // Requests
  {
    uint8_t d[8];
    request(PID_RPM, d);
    CHECK(d[0] == 2 && d[1] == 1 && d[2] == 0x0C && d[3] == 0x55 && d[7] == 0x55, "RPM request");
  }

  // Replies
  {
    Values v;
    const uint8_t rpm[8] = {0x04, 0x41, 0x0C, 0x1A, 0xF8, 0, 0, 0};  // 0x1AF8 / 4 = 1726 rpm
    CHECK(parse(0x7E8, rpm, 8, v, 1000) == PID_RPM && fabsf(v.rpm - 1726) < 0.01f && v.rpmAt == 1000, "rpm %.1f", v.rpm);
    const uint8_t thr[8] = {0x03, 0x41, 0x11, 0xFF, 0, 0, 0, 0};
    CHECK(parse(0x7E9, thr, 8, v, 1010) == PID_THROTTLE && fabsf(v.throttlePct - 100) < 0.01f, "throttle %.1f", v.throttlePct);
    const uint8_t cool[8] = {0x03, 0x41, 0x05, 0x7B, 0, 0, 0, 0};  // 123 - 40 = 83 C
    CHECK(parse(0x7E8, cool, 8, v, 1020) == PID_COOLANT && fabsf(v.coolantC - 83) < 0.01f, "coolant %.1f", v.coolantC);
    const uint8_t cold[8] = {0x03, 0x41, 0x05, 0x00, 0, 0, 0, 0};
    CHECK(parse(0x7E8, cold, 8, v, 1030) == PID_COOLANT && fabsf(v.coolantC + 40) < 0.01f, "-40 C");

    // Everything else on the bus is ignored, and leaves the values alone
    const float before = v.rpm;
    CHECK(parse(0x7DF, rpm, 8, v, 2000) == -1, "our own request's id");
    CHECK(parse(0x123, rpm, 8, v, 2000) == -1, "some other ECU's message");
    const uint8_t mode9[8] = {0x04, 0x49, 0x0C, 0x1A, 0xF8, 0, 0, 0};
    CHECK(parse(0x7E8, mode9, 8, v, 2000) == -1, "another mode");
    const uint8_t neg[8] = {0x03, 0x7F, 0x01, 0x12, 0, 0, 0, 0};
    CHECK(parse(0x7E8, neg, 8, v, 2000) == -1, "a 'not supported' reply");
    const uint8_t shortRpm[8] = {0x03, 0x41, 0x0C, 0x1A, 0, 0, 0, 0};
    CHECK(parse(0x7E8, shortRpm, 8, v, 2000) == -1, "RPM with one byte");
    CHECK(parse(0x7E8, rpm, 3, v, 2000) == -1, "a frame cut short");
    const uint8_t first[8] = {0x10, 0x14, 0x49, 0x02, 0x01, 0x31, 0x47, 0x31};
    CHECK(parse(0x7E8, first, 8, v, 2000) == -1, "a multi-frame message");
    CHECK(v.rpm == before && v.rpmAt == 1000, "unchanged");

    // Stale after a second without news
    CHECK(v.rpmKnown(1500) && v.rpmKnown(2000) && !v.rpmKnown(2001), "stale after STALE_MS");
    Values none;
    CHECK(!none.rpmKnown(0) && !none.throttleKnown(500) && !none.coolantKnown(100000), "never read: unknown");
    CHECK(parse(0x7E8, rpm, 8, none, 0) == PID_RPM && none.rpmKnown(5), "read at time 0 still counts");
  }

  // The schedule: RPM every other request, one at a time
  {
    Poller p;
    uint32_t t = 0;
    int counts[256] = {0};
    for (int i = 0; i < 40; i++) {
      int pid = -1;
      while ((pid = p.due(t)) < 0) t++;
      counts[pid]++;
      p.sent(t);
      CHECK(p.due(t + 5) == -1, "waits for the reply");
      t += 12;  // the engine answers 12 ms later
      p.replied(pid);
      CHECK(p.due(t) == -1, "and keeps a gap between requests");
    }
    CHECK(counts[PID_RPM] == 20 && counts[PID_THROTTLE] == 10 && counts[PID_COOLANT] == 10, "rpm %d throttle %d coolant %d",
          counts[PID_RPM], counts[PID_THROTTLE], counts[PID_COOLANT]);
    CHECK(t < 40 * 45, "about 25 a second: %u ms for 40", (unsigned)t);
    CHECK(p.quietCount() == 0, "answered");

    // No answer: the next one after the timeout, and it counts the silence
    Poller q;
    q.sent(1000);
    CHECK(q.due(1000 + REPLY_TIMEOUT_MS - 1) == -1 && q.due(1000 + REPLY_TIMEOUT_MS) >= 0, "times out");
    for (int i = 1; i < 10; i++) q.sent(1000 + i * 200);
    CHECK(q.quietCount() == 10, "10 unanswered: %d", q.quietCount());
    q.replied(PID_THROTTLE);
    CHECK(q.quietCount() == 0, "an answer resets it");
  }

  // Shift light
  {
    Rgb bar[10];
    CHECK(!shiftLights(4000, 5500, 6800, 0, bar, 10), "below the start: the delta shows");
    CHECK(shiftLights(5500, 5500, 6800, 0, bar, 10), "on at the start");
    int lit = 0;
    for (const Rgb& c : bar) lit += c != Rgb{0, 0, 0};
    CHECK(lit == 1 && bar[0] == SHIFT_GREEN, "one green at the start: %d", lit);
    shiftLights(6700, 5500, 6800, 0, bar, 10);
    lit = 0;
    for (const Rgb& c : bar) lit += c != Rgb{0, 0, 0};
    CHECK(lit == 10 && bar[0] == SHIFT_GREEN && bar[6] == SHIFT_YELLOW && bar[9] == SHIFT_RED, "nearly full: %d", lit);
    shiftLights(6150, 5500, 6800, 0, bar, 10);
    lit = 0;
    for (const Rgb& c : bar) lit += c != Rgb{0, 0, 0};
    CHECK(lit == 6, "half way: %d", lit);
    CHECK(shiftLights(7000, 5500, 6800, 0, bar, 10) && bar[0] == SHIFT_FLASH && bar[9] == SHIFT_FLASH, "shift: all blue");
    shiftLights(7000, 5500, 6800, SHIFT_FLASH_MS / 2, bar, 10);
    CHECK(bar[0] == (Rgb{0, 0, 0}), "flashing");
    CHECK(!shiftLights(6000, 6000, 6000, 0, bar, 10), "a bad range is off");
  }

  if (failures) {
    printf("%d FAILED\n", failures);
    return 1;
  }
  printf("All OBD-II tests passed\n");
  return 0;
}
