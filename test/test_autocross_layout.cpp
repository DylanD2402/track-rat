// The Autocross hub's hit zones match the drawn buttons, do not overlap and stay on the screen.
#include <cstdio>
#include "AutocrossLayout.h"
using namespace autocross_layout;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main() {
  for (int b = 0; b < COUNT; b++) {
    const Rect r = button(b), z = zone(b);
    CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= SCREEN_W && r.y + r.h <= SCREEN_H, "button %d off screen", b);
    CHECK(z.x >= 0 && z.y >= 0 && z.x + z.w <= SCREEN_W && z.y + z.h <= SCREEN_H, "zone %d off screen", b);
    // every drawn pixel of the button, and its centre, hits that button
    for (int y = r.y; y < r.y + r.h; y++)
      for (int x = r.x; x < r.x + r.w; x++) CHECK(buttonAt(x, y) == b, "button %d pixel %d,%d -> %d", b, x, y, buttonAt(x, y));
    // the zone reaches no more than 6 px past the drawn button (half the gaps, and the 2 px above the top row)
    CHECK(r.x - z.x <= 6 && r.y - z.y <= 6 && (z.x + z.w) - (r.x + r.w) <= 6, "zone %d too big", b);
  }
  // zones do not overlap, and every point gets at most one button
  for (int y = 0; y < SCREEN_H; y++)
    for (int x = 0; x < SCREEN_W; x++) {
      int n = 0;
      for (int b = 0; b < COUNT; b++) n += zone(b).contains(x, y);
      CHECK(n <= 1, "point %d,%d is in %d zones", x, y, n);
    }
  // the old mismatch: the row 2 button starts at y 194, so 191..193 is its zone but 150 is not above row 1
  CHECK(buttonAt(10, 149) == -1, "above the top row");
  CHECK(buttonAt(10, 150) == START_RUNS, "top row from y 150");
  CHECK(buttonAt(10, 239) == ADJUST_START, "bottom edge");
  if (!fails) printf("autocross layout: all passed\n");
  return fails ? 1 : 0;
}
