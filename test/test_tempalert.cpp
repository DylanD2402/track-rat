/**
 * Host-side tests for the engine's temperatures on screen (include/TempAlert.h):
 * the coolant tag's colour bands, degrees F, and when the full-screen
 * overheat card shows (rising through the limit, hysteresis, the 60 s repeat,
 * stale data, the oil limit, a tap, the LED flash).
 *
 *   g++ -std=c++17 -Iinclude test/test_tempalert.cpp -o /tmp/ta && /tmp/ta
 */

#include <cstdio>
#include "TempAlert.h"
#include "check.h"

using namespace tempalert;

static void testBands() {
  const Limits l;  // 70 / 100 / 110, oil 130
  CHECK(l.coldBelowC == 70 && l.warmAtC == 100 && l.hotAtC == 110 && l.oilHotAtC == 130, "defaults from Config.h");
  // Rising from a cold start through every band
  struct { float c; Band b; } rise[] = {{20, Band::Cold}, {69.9f, Band::Cold}, {70, Band::Normal}, {92, Band::Normal},
                                        {99.9f, Band::Normal}, {100, Band::Warm}, {109, Band::Warm}, {110, Band::Hot},
                                        {125, Band::Hot}, {-40, Band::Cold}};
  for (auto& r : rise) CHECK(coolantBand(r.c, l) == r.b, "%.1f C: band %d, want %d", r.c, (int)coolantBand(r.c, l), (int)r.b);
  Limits m;
  m.coldBelowC = 50, m.warmAtC = 90, m.hotAtC = 95;
  CHECK(coolantBand(60, m) == Band::Normal && coolantBand(92, m) == Band::Warm && coolantBand(96, m) == Band::Hot, "other limits");
  CHECK(oilBand(129, l) == Band::Normal && oilBand(130, l) == Band::Hot && oilBand(20, l) == Band::Normal, "oil: hot or not");
  printf("  bands: cold, normal, warm, hot, at the limits\n");
}

static void testDegrees() {
  CHECK(degrees(92, false) == 92 && degrees(91.6f, false) == 92 && degrees(-40, false) == -40, "C rounded");
  CHECK(degrees(100, true) == 212 && degrees(0, true) == 32 && degrees(-40, true) == -40 && degrees(110, true) == 230 &&
            degrees(70, true) == 158 && degrees(130, true) == 266,
        "F: 212, 32, -40, 230, 158, 266");
  CHECK(degrees(101, true) == 214 && degrees(102, true) == 216, "1 C steps show as about 2 F");
  printf("  degrees: C, and F for mph\n");
}

// Feeds a temperature every 100 ms from `from` to `to` (inclusive, linear); counts the cards started
static int ramp(Alert& a, uint32_t& t, float from, float to, uint32_t ms, const Limits& l, bool known = true, bool oil = false) {
  int cards = 0;
  for (uint32_t k = 0; k <= ms; k += 100, t += 100) {
    const float c = from + (to - from) * k / (ms ? ms : 1);
    cards += a.update(t, !oil && known, c, oil && known, c, l);
  }
  return cards;
}

static void testCard() {
  const Limits l;
  {  // Warming up: one card on reaching Hot at, for TEMP_ALERT_MS
    Alert a;
    uint32_t t = 1000;
    CHECK(ramp(a, t, 60, 109.9f, 20000, l) == 0 && !a.showing(t), "no card below Hot at");
    CHECK(a.update(t, true, 110, false, 0, l) && a.showing(t) && !a.current().oil && a.current().c == 110, "the card at 110");
    const uint32_t start = t;
    CHECK(!a.update(t + 1000, true, 112, false, 0, l) && a.current().c == 112, "it follows the temperature while it shows");
    CHECK(a.showing(start + TEMP_ALERT_MS - 1) && !a.showing(start + TEMP_ALERT_MS), "for TEMP_ALERT_MS");
    t = start + TEMP_ALERT_MS;
    CHECK(ramp(a, t, 112, 113, 30000, l) == 0, "still hot: not again within a minute");
    // Hysteresis: a dip that isn't TEMP_REARM_DROP_C below doesn't count
    CHECK(ramp(a, t, 113, 108, 3000, l) == 0 && ramp(a, t, 108, 111, 3000, l) == 0, "a dip to 108 doesn't re-arm");
    CHECK(ramp(a, t, 111, 107, 3000, l) == 0, "down to 107 (3 below): re-armed, no card");
    CHECK(ramp(a, t, 107, 110, 3000, l) == 1, "back up to 110: a card");
  }
  {  // Over the limit for a long time: once a minute
    Alert a;
    uint32_t t = 5000;
    const int cards = ramp(a, t, 115, 115, 185000, l);  // 3 min 5 s
    CHECK(cards == 4, "at 0, 60, 120 and 180 s: %d", cards);
  }
  {  // Stale data: nothing, and it doesn't re-arm
    Alert a;
    uint32_t t = 0;
    CHECK(ramp(a, t, 120, 120, 5000, l, false) == 0 && !a.showing(t), "not known: no card");
    CHECK(ramp(a, t, 120, 120, 100, l) == 1, "known again: the card");
    t += TEMP_ALERT_MS;
    CHECK(ramp(a, t, 20, 20, 2000, l, false) == 0 && ramp(a, t, 120, 120, 1000, l) == 0, "a stale 20 C isn't a drop");
    // A card whose temperature goes stale keeps the last value it had
    Alert b;
    uint32_t u = 0;
    b.update(u, true, 111, false, 0, l);
    b.update(u + 500, false, 0, false, 0, l);
    CHECK(b.showing(u + 500) && b.current().c == 111, "stale while showing: the last value stays");
  }
  {  // Oil, with its own limit
    Alert a;
    uint32_t t = 0;
    CHECK(ramp(a, t, 100, 129, 5000, l, true, true) == 0, "oil to 129: nothing (coolant's 110 isn't oil's)");
    CHECK(ramp(a, t, 129, 131, 1000, l, true, true) == 1 && a.current().oil && a.current().c >= 130, "oil at 130: the OIL card");
    // Both over: coolant first, then oil once its card is done
    Alert b;
    uint32_t u = 0;
    CHECK(b.update(u, true, 115, true, 140, l) && !b.current().oil, "both: coolant first");
    int oilCards = 0;
    for (u = 100; u <= TEMP_ALERT_MS + 500; u += 100) oilCards += b.update(u, true, 115, true, 140, l) && b.current().oil;
    CHECK(oilCards == 1 && b.current().oil && b.current().c == 140, "then oil");
    Limits m;
    m.oilHotAtC = 150;
    Alert c;
    CHECK(!c.update(0, false, 0, true, 140, m) && c.update(100, false, 0, true, 150, m), "Oil hot at changed to 150");
  }
  {  // A tap ends it early; it doesn't come back until re-armed or a minute on
    Alert a;
    a.update(0, true, 111, false, 0, l);
    a.dismiss();
    CHECK(!a.showing(1), "dismissed");
    CHECK(!a.update(500, true, 111, false, 0, l) && !a.showing(500), "not straight back");
    CHECK(a.update(TEMP_REPEAT_MS, true, 111, false, 0, l), "a minute on, still hot: again");
  }
  {  // The LED bar flashes red in CAM_REC_BLINK_MS halves while it shows
    Alert a;
    a.update(1000, true, 111, false, 0, l);
    CHECK(a.ledRed(1000) && a.ledRed(1000 + CAM_REC_BLINK_MS - 1) && !a.ledRed(1000 + CAM_REC_BLINK_MS) &&
              a.ledRed(1000 + 2 * CAM_REC_BLINK_MS),
          "on, off, on");
    CHECK(!a.ledRed(1000 + TEMP_ALERT_MS) && !Alert().ledRed(0), "off when it's gone, and before any card");
  }
  {  // Raising Hot at while hot re-arms through the drop rule only
    Alert a;
    uint32_t t = 0;
    ramp(a, t, 111, 111, 100, l);
    Limits up = l;
    up.hotAtC = 120;
    t += TEMP_ALERT_MS;
    CHECK(ramp(a, t, 111, 111, 2000, up) == 0, "under the new limit: nothing");
    CHECK(ramp(a, t, 111, 121, 2000, up) == 1, "over it: a card (111 was 9 below 120)");
  }
  printf("  card: once at the limit, 3 below to re-arm, once a minute, stale, oil, tap, LED\n");
}

int main() {
  printf("Temperature alert tests\n");
  testBands();
  testDegrees();
  testCard();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nAll temperature alert tests passed\n", failures);
  return failures ? 1 : 0;
}
