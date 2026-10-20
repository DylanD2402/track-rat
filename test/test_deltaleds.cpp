/**
 * Host-side tests for the LED delta pattern and add-on pin check.
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_deltaleds.cpp src/DeltaLeds.cpp src/LapTimer.cpp -o /tmp/dl && /tmp/dl
 */

#include <cstdio>
#include <cmath>
#include <cstring>
#include "AddOns.h"
#include "DeltaLeds.h"
#include "LapTimer.h"
#include "check.h"

static bool isOff(const Rgb& c) { return c.r == 0 && c.g == 0 && c.b == 0; }

// "..GGG" style picture of the bar: '.' off, 'G' green, 'R' red, 'P' purple, 'W' white, 'a' amber, 'g' dim green
static void picture(const Rgb* bar, int n, char* out) {
  for (int i = 0; i < n; i++) {
    const Rgb& c = bar[i];
    out[i] = c == LED_AHEAD ? 'G' : c == LED_BEHIND ? 'R' : c == LED_BEST ? 'P' : c == LED_SPLIT ? 'W'
           : c == LED_IDLE ? 'a' : c == LED_ON_PACE ? 'g' : isOff(c) ? '.' : '?';
  }
  out[n] = 0;
}

static void show(DeltaLedPattern& p, uint32_t now, bool timing, bool has, int32_t delta, char* out, Rgb* board = nullptr) {
  Rgb bar[LED_MAX_COUNT], b;
  p.setDelta(timing, has, delta);
  p.render(now, bar, b);
  picture(bar, p.ledCount(), out);
  if (board) *board = b;
}

int main() {
  printf("DeltaLeds tests\n");
  char pic[LED_MAX_COUNT + 1];
  Rgb board;

  DeltaLedPattern p;
  p.begin(10, 1000, false);

  // Ahead (negative delta) grows right in green; behind grows left in red; +/-1 s fills a side.
  show(p, 0, true, true, -1000, pic);   CHECK(!strcmp(pic, ".....GGGGG"), "ahead 1 s: %s", pic);
  show(p, 0, true, true, 1000, pic);    CHECK(!strcmp(pic, "RRRRR....."), "behind 1 s: %s", pic);
  show(p, 0, true, true, -5000, pic);   CHECK(!strcmp(pic, ".....GGGGG"), "ahead 5 s clamps: %s", pic);
  show(p, 0, true, true, 5000, pic);    CHECK(!strcmp(pic, "RRRRR....."), "behind 5 s clamps: %s", pic);
  show(p, 0, true, true, -200, pic);    CHECK(!strcmp(pic, ".....G...."), "ahead 0.2 s: %s", pic);
  show(p, 0, true, true, -201, pic);    CHECK(!strcmp(pic, ".....GG..."), "ahead 0.201 s: %s", pic);
  show(p, 0, true, true, 600, pic);     CHECK(!strcmp(pic, "..RRR....."), "behind 0.6 s: %s", pic);
  show(p, 0, true, true, 5, pic, &board);
  CHECK(!strcmp(pic, "....gg...."), "on the pace: %s", pic);
  printf("  scaling and sides: ok\n");

  // Monotonic: more delta never lights fewer LEDs
  int prev = 0;
  for (int32_t d = 0; d <= 1500; d += 10) {
    int l = p.litCount(d);
    CHECK(l >= prev, "litCount not monotonic at %d", (int)d);
    CHECK(l == p.litCount(-d), "litCount not symmetric at %d", (int)d);
    prev = l;
  }
  CHECK(p.litCount(1500) == 5, "full side is 5");

  // No delta / not timing
  show(p, 0, true, false, 0, pic, &board);
  CHECK(!strcmp(pic, "....aa...."), "timing, no delta: %s", pic);
  CHECK(isOff(board), "board off with no delta");
  show(p, 0, false, true, -500, pic, &board);
  CHECK(!strcmp(pic, ".........."), "not timing: %s", pic);
  CHECK(isOff(board), "board off when not timing");

  // Board LED mirrors the delta colour
  show(p, 0, true, true, -1000, pic, &board);
  CHECK(board.g > 0 && board.r == 0 && board.b == 0, "board green when ahead");
  Rgb ahead1 = board;
  show(p, 0, true, true, 1000, pic, &board);
  CHECK(board.r > 0 && board.g == 0 && board.b == 0, "board red when behind");
  show(p, 0, true, true, -100, pic, &board);
  CHECK(board.g > 0 && board.g < ahead1.g, "board dimmer for a small gap");
  printf("  board LED mirrors delta: ok\n");

  // Odd LED count: the centre LED is a marker, sides are n/2
  DeltaLedPattern odd;
  odd.begin(9, 1000, false);
  show(odd, 0, true, true, -1000, pic);  CHECK(!strcmp(pic, ".....GGGG"), "odd ahead: %s", pic);
  show(odd, 0, true, true, 1000, pic);   CHECK(!strcmp(pic, "RRRR.....") , "odd behind: %s", pic);
  show(odd, 0, true, false, 0, pic);     CHECK(!strcmp(pic, "....a...."), "odd idle: %s", pic);

  // Reversed strip flips sides
  DeltaLedPattern rev;
  rev.begin(10, 1000, true);
  show(rev, 0, true, true, -1000, pic);  CHECK(!strcmp(pic, "GGGGG....."), "reversed ahead: %s", pic);
  show(rev, 0, true, true, 1000, pic);   CHECK(!strcmp(pic, ".....RRRRR"), "reversed behind: %s", pic);

  // Sizes are clamped
  DeltaLedPattern big;
  big.begin(500);
  CHECK(big.ledCount() == LED_MAX_COUNT, "count clamps high");
  big.begin(0);
  CHECK(big.ledCount() == 2, "count clamps low");

  // Best lap: purple blinks for LED_BEST_FLASH_MS, then back to the delta
  DeltaLedPattern e;
  e.begin(10, 1000, false);
  e.bestLap(10000);
  show(e, 10000, true, true, 500, pic, &board);
  CHECK(!strcmp(pic, "PPPPPPPPPP"), "best flash on: %s", pic);
  CHECK(board == LED_BEST, "board purple on best lap");
  show(e, 10000 + LED_BEST_BLINK_MS + 10, true, true, 500, pic);
  CHECK(!strcmp(pic, ".........."), "best flash dark half: %s", pic);
  show(e, 10000 + 2 * LED_BEST_BLINK_MS + 10, true, true, 500, pic);
  CHECK(!strcmp(pic, "PPPPPPPPPP"), "best flash second blink: %s", pic);
  show(e, 10000 + LED_BEST_FLASH_MS, true, true, 500, pic);
  CHECK(!strcmp(pic, "..RRR....."), "after the flash: %s", pic);

  // Split: short white blink, then back
  e.split(20000);
  show(e, 20000 + 50, true, true, -1000, pic, &board);
  CHECK(!strcmp(pic, "WWWWWWWWWW"), "split blink: %s", pic);
  CHECK(board == LED_SPLIT, "board white at a split");
  show(e, 20000 + LED_SPLIT_BLINK_MS, true, true, -1000, pic);
  CHECK(!strcmp(pic, ".....GGGGG"), "after the split blink: %s", pic);

  // Best-lap flash wins over a split that lands inside it
  e.bestLap(30000);
  e.split(30100);
  show(e, 30100, true, true, 0, pic);
  CHECK(!strcmp(pic, "PPPPPPPPPP"), "best flash over split: %s", pic);

  // A best lap that isn't a PB flashes green instead
  e.bestLap(40000, LED_SESSION_BEST);
  show(e, 40000, true, true, 0, pic, &board);
  CHECK(board == LED_SESSION_BEST && pic[0] != 'P', "session best: green, not purple (%s)", pic);

  // millis() wrap in the middle of a flash
  DeltaLedPattern w;
  w.begin(10);
  w.bestLap(0xFFFFFF00u);
  show(w, 0xFFFFFF00u + 100, true, true, 0, pic);   // wraps past 0
  CHECK(!strcmp(pic, "PPPPPPPPPP"), "flash across millis wrap: %s", pic);
  show(w, 0xFFFFFF00u + LED_BEST_FLASH_MS + 5, true, true, 0, pic);
  CHECK(strcmp(pic, "PPPPPPPPPP") != 0, "flash ends across wrap: %s", pic);
  printf("  best-lap flash, split blink, priority, clock wrap: ok\n");

  // Add-on pins vs where the GPS was found
  CHECK(!addOnPinTaken(21, 44, 43), "UART connector leaves IO21 free");
  CHECK(!addOnPinTaken(2, 44, 43), "UART connector leaves IO2 free");
  CHECK(addOnPinTaken(21, 14, 21), "GPS on 14/21 takes IO21");
  CHECK(addOnPinTaken(21, 21, 14), "GPS on 21/14 takes IO21");
  CHECK(!addOnPinTaken(2, 14, 21), "GPS on 14/21 leaves IO2 free");
  CHECK(addOnPinTaken(2, 2, 3), "GPS on IO2 takes IO2");
  CHECK(!addOnPinTaken(-1, 14, 21), "no pin is never taken");
  printf("  add-on pin collisions: ok\n");

  // Failure paths: extreme and zero settings never crash or light a wrong LED
  show(p, 0, true, true, INT32_MIN, pic);  CHECK(!strcmp(pic, ".....GGGGG"), "INT32_MIN: %s", pic);
  show(p, 0, true, true, INT32_MAX, pic);  CHECK(!strcmp(pic, "RRRRR....."), "INT32_MAX: %s", pic);
  DeltaLedPattern z;
  z.begin(10, 0, false);
  show(z, 0, true, true, 100, pic);        CHECK(strlen(pic) == 10, "zero full scale survives: %s", pic);
  DeltaLedPattern two;
  two.begin(2);
  show(two, 0, true, true, -1000, pic);    CHECK(!strcmp(pic, ".G"), "2 LEDs ahead: %s", pic);
  show(two, 0, true, true, 1000, pic);     CHECK(!strcmp(pic, "R."), "2 LEDs behind: %s", pic);
  // Events fired before any render, and a stale flash after the session ends, are harmless
  DeltaLedPattern s2;
  s2.begin(10);
  s2.split(5);
  show(s2, 100000, false, false, 0, pic);  CHECK(!strcmp(pic, ".........."), "old split ignored: %s", pic);

  // End to end: synthetic GPS laps -> LapTimer -> pattern, as main.cpp wires them.
  {
    const double lat0 = 37.0, lon0 = -122.0, R = 150.0;
    const double mLat = 111194.93, mLon = 111194.93 * cos(lat0 * geo::DEG2RAD);
    LapTimer lt;
    lt.begin({lat0, lon0, 15}, {{lat0, lon0 - 2 * R / mLon, 15}});
    DeltaLedPattern sim;
    sim.begin(10);
    double speeds[] = {30.0, 35.0, 25.0, 30.0};  // lap 2 faster, lap 3 slower
    double angle = -0.2, t = 0;
    uint32_t now = 1000;
    bool sawPurple = false, sawWhite = false, sawGreenRight = false, sawRedLeft = false;
    int purplesByLap[5] = {0}, whitesByLap[5] = {0};
    while (lt.lapCount() < 4 && t < 400000) {
      double v = speeds[lt.lapCount() < 4 ? lt.lapCount() : 3];
      double lat = lat0 + R * sin(angle) / mLat, lon = lon0 + R * (cos(angle) - 1.0) / mLon;
      for (const LapEvent& ev : lt.onFix(lat, lon, v * 3.6f, (uint32_t)t, now)) {
        if (ev.type == LapEventType::LapCompleted && ev.isBest) sim.bestLap(now);
        if (ev.type == LapEventType::SplitCrossed) sim.split(now);
      }
      int32_t d = lt.liveDeltaMs();
      // render 5 frames per GPS fix (the driver runs faster than the GPS)
      for (int k = 0; k < 5; k++) {
        show(sim, now + k * 10, lt.lapRunning(), d != NO_DELTA, d, pic);
        int lap = (int)lt.lapCount();
        if (strchr(pic, 'P')) { sawPurple = true; purplesByLap[lap]++; }
        if (strchr(pic, 'W')) { sawWhite = true; whitesByLap[lap]++; }
        if (lt.lapCount() == 1 && d != NO_DELTA && d < -300 && pic[9] == 'G' && pic[0] == '.') sawGreenRight = true;
        if (lt.lapCount() == 2 && d != NO_DELTA && d > 300 && pic[0] == 'R' && pic[9] == '.') sawRedLeft = true;
        if (d != NO_DELTA && !strchr(pic, 'P') && !strchr(pic, 'W')) {
          bool left = strchr(pic, 'R') != nullptr, right = strchr(pic, 'G') != nullptr;
          CHECK(!(left && right), "both sides lit: %s (delta %d)", pic, d);
          if (d <= -LED_DEADBAND_MS) CHECK(!left, "red while ahead: %s (delta %d)", pic, d);
          if (d >= LED_DEADBAND_MS) CHECK(!right, "green while behind: %s (delta %d)", pic, d);
        }
      }
      angle += v * 0.05 / R;
      t += 50;
      now += 50;
    }
    CHECK(lt.lapCount() == 4, "simulated %zu laps", lt.lapCount());
    CHECK(sawPurple, "purple on a best lap");
    CHECK(sawWhite, "white at a split");
    CHECK(purplesByLap[3] == 0, "no purple on the slower lap 3 (flash window passed): %d", purplesByLap[3]);
    CHECK(sawGreenRight, "green on the right while ahead on lap 2");
    CHECK(sawRedLeft, "red on the left while behind on lap 3");
    printf("  20 Hz synthetic laps through LapTimer: purple=%d white=%d green-right=%d red-left=%d\n", sawPurple,
           sawWhite, sawGreenRight, sawRedLeft);
  }

  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
