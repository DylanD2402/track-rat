// PC stand-in for TFT_eSPI: see TFT_eSPI.h.
#include "TFT_eSPI.h"

static int32_t readInt32(const uint8_t* p) { return (int32_t)((uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); }

static bool insideRound(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r) {
  if (px < x || py < y || px >= x + w || py >= y + h) return false;
  if (r <= 0) return true;
  int32_t cx = px < x + r ? x + r : px > x + w - 1 - r ? x + w - 1 - r : px;
  int32_t cy = py < y + r ? y + r : py > y + h - 1 - r ? y + h - 1 - r : py;
  int32_t dx = px - cx, dy = py - cy;
  return dx * dx + dy * dy <= r * r + r;
}

void TFT_eSPI::roundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, bool fill) {
  r = std::min(r, std::min(w, h) / 2);
  for (int32_t py = y; py < y + h; py++)
    for (int32_t px = x; px < x + w; px++) {
      if (!insideRound(px, py, x, y, w, h, r)) continue;
      if (fill || !insideRound(px, py, x + 1, y + 1, w - 2, h - 2, r - 1)) drawPixel(px, py, c);
    }
}

// Smooth_font.cpp loadFont() / loadMetrics(), reading the .vlw array
void TFT_eSPI::loadFont(const uint8_t* data) {
  int32_t count = readInt32(data);
  ascent_ = (int16_t)readInt32(data + 16);
  descent_ = (int16_t)readInt32(data + 20);
  maxAscent_ = ascent_;
  maxDescent_ = descent_;
  glyphs_.clear();
  const uint8_t* meta = data + 24;
  const uint8_t* bitmap = meta + count * 28;
  for (int32_t i = 0; i < count; i++, meta += 28) {
    Glyph g;
    g.code = (uint16_t)readInt32(meta);
    g.h = (uint8_t)readInt32(meta + 4);
    g.w = (uint8_t)readInt32(meta + 8);
    g.xAdvance = (uint8_t)readInt32(meta + 12);
    g.dY = (int16_t)readInt32(meta + 16);
    g.dX = (int8_t)readInt32(meta + 20);
    g.bitmap = bitmap;
    bitmap += g.w * g.h;
    if (((g.code > 0x20 && g.code < 0xA0 && g.code != 0x7F) || g.code > 0xFF) && g.h - g.dY > maxDescent_)
      maxDescent_ = g.h - g.dY;
    glyphs_.push_back(g);
  }
  yAdvance_ = maxAscent_ + maxDescent_;
  spaceWidth_ = (ascent_ + descent_) * 2 / 7;
  loaded_ = true;
}

bool TFT_eSPI::glyphIndex(uint16_t code, size_t* i) const {
  for (size_t k = 0; k < glyphs_.size(); k++)
    if (glyphs_[k].code == code) {
      *i = k;
      return true;
    }
  return false;
}

static uint16_t nextUtf8(const uint8_t* s, size_t* n, size_t len) {
  uint8_t c = s[(*n)++];
  if ((c & 0x80) == 0) return c;
  if ((c & 0xE0) == 0xC0 && *n < len) return ((c & 0x1F) << 6) | (s[(*n)++] & 0x3F);
  if ((c & 0xF0) == 0xE0 && *n + 1 < len) {
    uint16_t u = ((c & 0x0F) << 12) | ((s[*n] & 0x3F) << 6) | (s[*n + 1] & 0x3F);
    *n += 2;
    return u;
  }
  return c;
}

int16_t TFT_eSPI::textWidth(const char* s) {
  if (!loaded_) return 0;
  int32_t width = 0;
  size_t n = 0, len = strlen(s);
  while (n < len) {
    uint16_t code = nextUtf8((const uint8_t*)s, &n, len);
    if (code == 0x20) {
      width += spaceWidth_;
      continue;
    }
    size_t i;
    if (glyphIndex(code, &i)) {
      const Glyph& g = glyphs_[i];
      if (width == 0 && g.dX < 0) width -= g.dX;
      if (n < len) width += g.xAdvance;
      else width += g.dX + g.w;
    } else {
      fprintf(stderr, "render_screens: U+%04X is not in the font (text \"%s\")\n", code, s);
      width += spaceWidth_ + 1;
    }
  }
  return width;
}

int16_t TFT_eSPI::drawString(const char* s, int32_t x, int32_t y) {
  int32_t w = textWidth(s), h = yAdvance_, base = maxAscent_;
  switch (datum_) {
    case TC_DATUM: x -= w / 2; break;
    case TR_DATUM: x -= w; break;
    case ML_DATUM: y -= h / 2; break;
    case MC_DATUM: x -= w / 2, y -= h / 2; break;
    case MR_DATUM: x -= w, y -= h / 2; break;
    case BL_DATUM: y -= h; break;
    case BC_DATUM: x -= w / 2, y -= h; break;
    case BR_DATUM: x -= w, y -= h; break;
    case L_BASELINE: y -= base; break;
    case C_BASELINE: x -= w / 2, y -= base; break;
    case R_BASELINE: x -= w, y -= base; break;
  }
  cursorX_ = x, cursorY_ = y;
  size_t n = 0, len = strlen(s);
  while (n < len) drawGlyph(nextUtf8((const uint8_t*)s, &n, len));
  return w;
}

// Smooth_font.cpp drawGlyph(), without background filling (never used here)
void TFT_eSPI::drawGlyph(uint16_t code) {
  if (code == 0x20) {
    cursorX_ += spaceWidth_;
    return;
  }
  size_t i;
  if (!glyphIndex(code, &i)) {
    drawRect(cursorX_, cursorY_ + maxAscent_ - ascent_, spaceWidth_, ascent_, fg_);
    cursorX_ += spaceWidth_ + 1;
    return;
  }
  const Glyph& g = glyphs_[i];
  if (cursorX_ == 0) cursorX_ -= g.dX;
  int32_t cy = cursorY_ + maxAscent_ - g.dY, cx = cursorX_ + g.dX;
  for (int32_t y = 0; y < g.h; y++)
    for (int32_t x = 0; x < g.w; x++) {
      uint8_t a = g.bitmap[x + g.w * y];
      if (!a) continue;
      if (a == 0xFF) {
        drawPixel(cx + x, cy + y, fg_);
      } else {
        uint16_t bg = getColor_ ? getColor_(cx + x, cy + y) : bg_;
        drawPixel(cx + x, cy + y, alphaBlend(a, fg_, bg));
      }
    }
  cursorX_ += g.xAdvance;
}
