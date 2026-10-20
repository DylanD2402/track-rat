/**
 * Track Rat - LED delta pattern implementation
 */

#include "DeltaLeds.h"


void DeltaLedPattern::begin(int count, int32_t fullScaleMs, bool rev) {
  n = count < 2 ? 2 : (count > LED_MAX_COUNT ? LED_MAX_COUNT : count);
  fullScale = fullScaleMs < 1 ? 1 : fullScaleMs;
  reversed = rev;
}

void DeltaLedPattern::setDelta(bool isTiming, bool has, int32_t deltaMs) {
  timing = isTiming;
  hasDelta = has;
  delta = deltaMs;
}

int DeltaLedPattern::litCount(int32_t deltaMs) const {
  int64_t mag = deltaMs < 0 ? -(int64_t)deltaMs : deltaMs;
  if (mag < LED_DEADBAND_MS) return 0;
  int half = n / 2;
  // Round up, so any delta past the deadband shows at least one LED.
  int lit = (int)((mag * half + fullScale - 1) / fullScale);
  return lit > half ? half : lit;
}

void DeltaLedPattern::render(uint32_t nowMs, Rgb* bar, Rgb& board) {
  Rgb none = {0, 0, 0};
  for (int i = 0; i < n; i++) bar[i] = none;
  board = none;

  // Events win over the delta. Times are unsigned so a millis() wrap is harmless.
  if (bestActive) {
    uint32_t age = nowMs - bestStart;
    if (age >= LED_BEST_FLASH_MS) {
      bestActive = false;
    } else if (age % (2 * LED_BEST_BLINK_MS) < LED_BEST_BLINK_MS) {
      for (int i = 0; i < n; i++) bar[i] = bestColour;
      board = bestColour;
      return;
    } else {
      return;  // dark half of a blink
    }
  }
  if (splitActive) {
    if (nowMs - splitStart >= LED_SPLIT_BLINK_MS) {
      splitActive = false;
    } else {
      for (int i = 0; i < n; i++) bar[i] = LED_SPLIT;
      board = LED_SPLIT;
      return;
    }
  }

  if (!timing) return;

  // The middle of the strip: between two LEDs for an even count, on one for an odd count.
  int half = n / 2;
  int leftEnd = half - 1;               // last LED of the left side (index counts down from here)
  int rightStart = n - half;            // first LED of the right side
  if (!hasDelta) {
    if (n % 2) bar[half] = LED_IDLE;
    else bar[leftEnd] = bar[rightStart] = LED_IDLE;
    return;
  }

  int lit = litCount(delta);
  bool ahead = delta < 0;  // negative = faster than the best lap
  Rgb c = ahead ? aheadC : behindC;
  if (lit == 0) {
    // On the pace: a dim green centre marker.
    const Rgb& m = onPaceC;
    if (n % 2) bar[half] = m;
    else bar[leftEnd] = bar[rightStart] = m;
    board = aheadC;
    return;
  }
  bool toRight = ahead != reversed;
  for (int i = 0; i < lit; i++) bar[toRight ? rightStart + i : leftEnd - i] = c;

  // Board LED: same colour, brighter the bigger the gap (30% to 100%).
  int64_t mag = delta < 0 ? -(int64_t)delta : delta;
  int64_t cap = mag > fullScale ? fullScale : mag;
  int scale = 30 + (int)(70 * cap / fullScale);
  board = {(uint8_t)(c.r * scale / 100), (uint8_t)(c.g * scale / 100), (uint8_t)(c.b * scale / 100)};
}
