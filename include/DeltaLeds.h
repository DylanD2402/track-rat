/**
 * Track Rat - LED delta pattern
 *
 * Turns the live delta and lap events into colours for a strip of LEDs and
 * the board's own RGB LED. Lights grow from the middle of the bar: to the
 * right in green when ahead of the best lap, to the left in red when behind,
 * scaled so the whole side is lit at +/- fullScaleMs. A new best lap flashes
 * the bar purple, and each split blinks it white. setColours swaps green
 * and red for blue and orange.
 *
 * No Arduino dependencies: tested in test/test_deltaleds.cpp. Brightness is
 * applied by the driver (LedBar.cpp), not here.
 */

#ifndef DELTA_LEDS_H
#define DELTA_LEDS_H

#include <stdint.h>
#include "Config.h"

struct Rgb {  // a plain aggregate: the firmware builds as C++11
  uint8_t r, g, b;
  bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
  bool operator!=(const Rgb& o) const { return !(*this == o); }
};

constexpr int LED_MAX_COUNT = 64;
constexpr int32_t LED_DEADBAND_MS = 20;       // closer than this to zero = on the pace
constexpr uint32_t LED_BEST_FLASH_MS = 1800;  // flash after a new best lap: purple for a PB, green otherwise
constexpr uint32_t LED_BEST_BLINK_MS = 300;   // on for the first half of each blink
constexpr uint32_t LED_SPLIT_BLINK_MS = 180;  // white blink at a split

constexpr Rgb LED_AHEAD = {0, 255, 0};
constexpr Rgb LED_BEHIND = {255, 0, 0};
constexpr Rgb LED_BEST = {160, 0, 255};       // a personal best
constexpr Rgb LED_SESSION_BEST = {0, 255, 0};  // a best lap that isn't a PB
constexpr Rgb LED_SPLIT = {255, 255, 255};
constexpr Rgb LED_IDLE = {70, 40, 0};         // timing, but no delta: dim amber centre
constexpr Rgb LED_ON_PACE = {0, 40, 0};       // within the deadband: dim green centre
// The Delta colours setting's blue / orange (for red-green colour blindness)
constexpr Rgb LED_AHEAD_BLUE = {0, 80, 255};
constexpr Rgb LED_BEHIND_ORANGE = {255, 80, 0};
constexpr Rgb LED_ON_PACE_BLUE = {0, 12, 40};

class DeltaLedPattern {
public:
  // count LEDs (clamped to 2..LED_MAX_COUNT); reversed flips left and right.
  void begin(int count, int32_t fullScaleMs = LED_BAR_FULL_SCALE_MS, bool reversed = false);
  int ledCount() const { return n; }

  // timing: a lap is running. hasDelta/deltaMs: live gap to the best lap (+ = slower).
  void setDelta(bool timing, bool hasDelta, int32_t deltaMs);
  void bestLap(uint32_t nowMs, Rgb colour = LED_BEST) {
    bestStart = nowMs;
    bestActive = true;
    bestColour = colour;
  }
  void split(uint32_t nowMs) { splitStart = nowMs; splitActive = true; }
  void setColours(Rgb ahead, Rgb behind, Rgb onPace) {
    aheadC = ahead;
    behindC = behind;
    onPaceC = onPace;
  }
  Rgb aheadColour() const { return aheadC; }

  // How many LEDs light on one side for this delta (0 = on the pace).
  int litCount(int32_t deltaMs) const;

  // Fills bar[0..ledCount()-1] and the board LED colour.
  void render(uint32_t nowMs, Rgb* bar, Rgb& board);

private:
  int n = 10;
  int32_t fullScale = 1000;
  bool reversed = false;
  bool timing = false, hasDelta = false;
  int32_t delta = 0;
  bool bestActive = false, splitActive = false;
  uint32_t bestStart = 0, splitStart = 0;
  Rgb bestColour = LED_BEST;
  Rgb aheadC = LED_AHEAD, behindC = LED_BEHIND, onPaceC = LED_ON_PACE;
};

#endif  // DELTA_LEDS_H
