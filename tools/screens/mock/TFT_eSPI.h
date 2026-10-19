// A PC stand-in for TFT_eSPI (tools/render_screens.py): draws into memory so
// the display code can be built and its screens saved as images. The smooth
// font code (text placement, widths and anti-aliasing) follows TFT_eSPI
// 2.5.43's Smooth_font.cpp and drawString(), so text lands where it would on
// the device. Shapes are close but not pixel-identical.
#pragma once

#include <vector>
#include "Arduino.h"

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
#define TFT_BLUE 0x001F
#define TFT_YELLOW 0xFFE0
#define TFT_DARKGREY 0x7BEF

#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8
#define L_BASELINE 9
#define C_BASELINE 10
#define R_BASELINE 11

typedef uint16_t (*getColorCallback)(uint16_t x, uint16_t y);

class TFT_eSPI {
public:
  TFT_eSPI(int w = 320, int h = 240) : w_(w), h_(h), pix_(w * h, 0) {}
  virtual ~TFT_eSPI() {}

  void init() {}
  void setRotation(int) {}
  void invertDisplay(bool) {}
  void writecommand(uint8_t) {}
  int width() const { return w_; }
  int height() const { return h_; }
  uint16_t* pixels() { return pix_.data(); }

  void fillScreen(uint16_t c) { std::fill(pix_.begin(), pix_.end(), c); }
  void drawPixel(int32_t x, int32_t y, uint16_t c) {
    if (x >= 0 && y >= 0 && x < w_ && y < h_) pix_[y * w_ + x] = c;
  }
  uint16_t readPixel(int32_t x, int32_t y) {
    return (x >= 0 && y >= 0 && x < w_ && y < h_) ? pix_[y * w_ + x] : 0xFFFF;
  }
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) {
    for (int32_t j = y; j < y + h; j++)
      for (int32_t i = x; i < x + w; i++) drawPixel(i, j, c);
  }
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) {
    drawFastHLine(x, y, w, c);
    drawFastHLine(x, y + h - 1, w, c);
    drawFastVLine(x, y, h, c);
    drawFastVLine(x + w - 1, y, h, c);
  }
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c) {
    int32_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
      drawPixel(x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      int32_t e2 = 2 * err;
      if (e2 >= dy) err += dy, x0 += sx;
      if (e2 <= dx) err += dx, y0 += sy;
    }
  }
  void fillCircle(int32_t cx, int32_t cy, int32_t r, uint16_t c) {
    for (int32_t y = -r; y <= r; y++)
      for (int32_t x = -r; x <= r; x++)
        if (x * x + y * y <= r * r + r) drawPixel(cx + x, cy + y, c);
  }
  // Rounded rectangles: inside test against the corner circles
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c) { roundRect(x, y, w, h, r, c, true); }
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c) { roundRect(x, y, w, h, r, c, false); }

  // --- text ---
  void setTextDatum(uint8_t d) { datum_ = d; }
  void setTextColor(uint16_t c) { fg_ = bg_ = c; }
  void setTextColor(uint16_t c, uint16_t b, bool = false) { fg_ = c, bg_ = b; }
  void setCallback(getColorCallback cb) { getColor_ = cb; }
  void loadFont(const uint8_t* data);
  void unloadFont() { loaded_ = false; }
  int16_t fontHeight() const { return yAdvance_; }
  int16_t textWidth(const String& s) { return textWidth(s.c_str()); }
  int16_t textWidth(const char* s);
  int16_t drawString(const String& s, int32_t x, int32_t y) { return drawString(s.c_str(), x, y); }
  int16_t drawString(const char* s, int32_t x, int32_t y);

  static uint16_t alphaBlend(uint8_t alpha, uint16_t fgc, uint16_t bgc) {  // as TFT_eSPI
    uint32_t rxb = bgc & 0xF81F;
    rxb += ((fgc & 0xF81F) - rxb) * (alpha >> 2) >> 6;
    uint32_t xgx = bgc & 0x07E0;
    xgx += ((fgc & 0x07E0) - xgx) * alpha >> 8;
    return (rxb & 0xF81F) | (xgx & 0x07E0);
  }

protected:
  int w_, h_;
  std::vector<uint16_t> pix_;

private:
  void roundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, bool fill);
  void drawGlyph(uint16_t code);
  bool glyphIndex(uint16_t code, size_t* i) const;

  uint8_t datum_ = TL_DATUM;
  uint16_t fg_ = 0xFFFF, bg_ = 0xFFFF;
  getColorCallback getColor_ = nullptr;
  int32_t cursorX_ = 0, cursorY_ = 0;

  struct Glyph {
    uint16_t code;
    uint8_t h, w, xAdvance;
    int16_t dY;
    int8_t dX;
    const uint8_t* bitmap;
  };
  bool loaded_ = false;
  std::vector<Glyph> glyphs_;
  int16_t ascent_ = 0, descent_ = 0, maxAscent_ = 0, maxDescent_ = 0, yAdvance_ = 0, spaceWidth_ = 0;
};

class TFT_eSprite : public TFT_eSPI {
public:
  explicit TFT_eSprite(TFT_eSPI* parent) : TFT_eSPI(0, 0), parent_(parent) {}
  void setColorDepth(int) {}
  void* createSprite(int w, int h) {
    w_ = w, h_ = h;
    pix_.assign(w * h, 0);
    return pix_.data();
  }
  void deleteSprite() { pix_.clear(); }
  void* getPointer() { return pix_.data(); }
  void fillSprite(uint16_t c) { fillScreen(c); }
  void pushSprite(int32_t x, int32_t y) { pushSprite(x, y, 0, 0, w_, h_); }
  void pushSprite(int32_t x, int32_t y, int32_t sx, int32_t sy, int32_t sw, int32_t sh) {
    for (int32_t j = 0; j < sh; j++)
      for (int32_t i = 0; i < sw; i++) parent_->drawPixel(x + i, y + j, pix_[(sy + j) * w_ + sx + i]);
  }

private:
  TFT_eSPI* parent_;
};
