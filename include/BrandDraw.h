/**
 * Track Rat - draw a logo array (RGB565, from tools/png_to_logo.py) onto black.
 * Pixels equal to black are skipped, so the frame behind (already cleared) shows through.
 */
#ifndef BRAND_DRAW_H
#define BRAND_DRAW_H

#include <TFT_eSPI.h>
#include <pgmspace.h>

inline void drawLogoPixels(TFT_eSPI* g, const uint16_t* px, int w, int h, int x0, int y0) {
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const uint16_t c = pgm_read_word(&px[y * w + x]);
      if (c) g->drawPixel(x0 + x, y0 + y, c);
    }
}

#endif  // BRAND_DRAW_H
