/**
 * Track Rat - logo sweep effect (used by Splash.cpp)
 *
 * Pure C++ so it can also run on a PC to preview the animation.
 * Pixels are RGB565 in TFT_eSprite byte order (byte-swapped).
 */

#ifndef SPLASH_SWEEP_H
#define SPLASH_SWEEP_H

#include <math.h>
#include <stdint.h>

// RGB565 with sprite byte order (sprites store pixels byte-swapped)
inline uint16_t swap16(uint16_t v) { return (v >> 8) | (v << 8); }

inline uint16_t scale565(uint16_t c, uint16_t a256) {
  uint16_t r = ((c >> 11) * a256) >> 8;
  uint16_t g = (((c >> 5) & 0x3F) * a256) >> 8;
  uint16_t b = ((c & 0x1F) * a256) >> 8;
  return (r << 11) | (g << 5) | b;
}

// One frame of the sweep. progress 0..1 moves the edge across the logo.
// fadeIn: pixels behind (left of) the edge are shown; fade-out: hidden.
inline void renderSweep(const uint16_t* src, uint16_t* dst, int w, int h, float progress, bool fadeIn,
                        float phase) {
  const float BAND = 30.0f;  // width of the soft edge, px
  const float AMP = 22.0f;   // how far the edge ripples sideways, px
  const float WAVELEN = 55.0f;  // ripple length down the logo, px
  const float travel = w + BAND + 2 * AMP;
  float base = -AMP + progress * travel;

  for (int y = 0; y < h; y++) {
    float edge = base + AMP * sinf(y * (2.0f * (float)M_PI / WAVELEN) + phase);
    const uint16_t* s = src + y * w;
    uint16_t* d = dst + y * w;
    for (int x = 0; x < w; x++) {
      float a = (edge - x) / BAND;  // 1 = fully behind the edge, 0 = ahead of it
      a = a < 0 ? 0 : (a > 1 ? 1 : a);
      a = a * a * (3 - 2 * a);  // smoothstep
      if (!fadeIn) a = 1 - a;
      uint16_t a256 = (uint16_t)(a * 256.0f);
      if (a256 >= 256) d[x] = s[x];
      else if (a256 == 0) d[x] = 0;
      else d[x] = swap16(scale565(swap16(s[x]), a256));
    }
  }
}

#endif  // SPLASH_SWEEP_H
