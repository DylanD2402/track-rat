/**
 * Host-side tests for the LED delta pattern and add-on pin check.
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_deltaleds.cpp src/DeltaLeds.cpp -o /tmp/dl && /tmp/dl
 */

#include <cstdio>
#include <cstring>
#include "AddOns.h"
#include "DeltaLeds.h"

static int failures = 0;
#define CHECK(cond, ...)                         \
  do {                                           \
    if (!(cond)) {                               \
      failures++;                                \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                       \
      printf("\n");                              \
    }                                            \
  } while (0)

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

  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
