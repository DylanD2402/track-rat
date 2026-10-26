#pragma once
/**
 * Fold UTF-8 text down to what the display fonts can draw (tools/make_fonts.py: ASCII plus
 * EXTRA = ± ° · – — … ‹ › − × ¼ ½ ⅛). Accented letters become plain ones (é -> e), typographic
 * punctuation becomes ASCII, and everything the fonts and this table don't know (other
 * scripts, emoji, broken UTF-8) becomes one "?" per run, so a lost name still shows that
 * something was there instead of silently shrinking. No Arduino dependencies.
 */

#include <cstddef>
#include <cstdint>

namespace textfold {

// Latin-1 Supplement letters U+00C0..U+00FF; "" = not folded here (D7 is kept as ×).
inline const char* latin1(uint32_t c) {
  static const char* const T[64] = {
      "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",   // C0
      "D", "N", "O", "O", "O", "O", "O", "",   "O", "U", "U", "U", "U", "Y", "TH", "ss",  // D0
      "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",   // E0
      "d", "n", "o", "o", "o", "o", "o", "/",  "o", "u", "u", "u", "u", "y", "th", "y"};  // F0
  return T[c - 0xC0];
}

// Latin Extended-A U+0100..U+017F
inline const char* extA(uint32_t c) {
  static const char* const T[128] = {
      "A", "a", "A", "a", "A", "a", "C", "c", "C", "c", "C", "c", "C", "c", "D", "d",     // 100
      "D", "d", "E", "e", "E", "e", "E", "e", "E", "e", "E", "e", "G", "g", "G", "g",     // 110
      "G", "g", "G", "g", "H", "h", "H", "h", "I", "i", "I", "i", "I", "i", "I", "i",     // 120
      "I", "i", "IJ", "ij", "J", "j", "K", "k", "k", "L", "l", "L", "l", "L", "l", "L",   // 130
      "l", "L", "l", "N", "n", "N", "n", "N", "n", "n", "N", "n", "O", "o", "O", "o",     // 140
      "O", "o", "OE", "oe", "R", "r", "R", "r", "R", "r", "S", "s", "S", "s", "S", "s",   // 150
      "S", "s", "T", "t", "T", "t", "T", "t", "U", "u", "U", "u", "U", "u", "U", "u",     // 160
      "U", "u", "U", "u", "W", "w", "Y", "y", "Y", "Z", "z", "Z", "z", "Z", "z", "s"};    // 170
  return T[c - 0x100];
}

// What one code point folds to: the replacement text ("" = drop it), or nullptr when unknown.
// *keep is set for characters the fonts draw, which are copied through as they were.
inline const char* mapOne(uint32_t c, bool* keep) {
  *keep = false;
  if (c < 0x80) { *keep = true; return ""; }
  switch (c) {
    case 0xB0: case 0xB1: case 0xB7: case 0xBC: case 0xBD: case 0xD7:   // ° ± · ¼ ½ ×
    case 0x2013: case 0x2014: case 0x2026: case 0x2039: case 0x203A:    // – — … ‹ ›
    case 0x2212: case 0x215B:                                           // − ⅛
      *keep = true;
      return "";
    case 0xA0: case 0x202F: return " ";
    case 0xAB: case 0xBB: return "\"";
    case 0x2010: case 0x2011: case 0x2012: return "-";
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: return "'";
    case 0x201C: case 0x201D: case 0x201E: return "\"";
    case 0x2022: return "\xC2\xB7";   // bullet -> middle dot, which the fonts have
    case 0xAD: case 0x200B: case 0x200C: case 0x200D: case 0xFEFF: return "";   // invisible
    case 0x1E9E: return "SS";
  }
  if (c >= 0x2000 && c <= 0x200A) return " ";
  if (c >= 0xC0 && c <= 0xFF) {
    const char* r = latin1(c);
    if (*r) return r;
  }
  if (c >= 0x100 && c <= 0x17F) return extA(c);
  return nullptr;
}

// Decode one UTF-8 code point at s (before e). Returns the bytes used, or 0 for a broken
// sequence (stray continuation byte, truncated, overlong, surrogate, above U+10FFFF).
inline int decode(const uint8_t* s, const uint8_t* e, uint32_t* cp) {
  const uint8_t b = s[0];
  int n;
  uint32_t c, min;
  if (b < 0x80) { *cp = b; return 1; }
  if (b >= 0xC2 && b <= 0xDF) { n = 2; c = b & 0x1F; min = 0x80; }
  else if (b >= 0xE0 && b <= 0xEF) { n = 3; c = b & 0x0F; min = 0x800; }
  else if (b >= 0xF0 && b <= 0xF4) { n = 4; c = b & 0x07; min = 0x10000; }
  else return 0;
  if (e - s < n) return 0;
  for (int i = 1; i < n; i++) {
    if ((s[i] & 0xC0) != 0x80) return 0;
    c = (c << 6) | (s[i] & 0x3F);
  }
  if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return 0;
  *cp = c;
  return n;
}

}  // namespace textfold

// True when s is all ASCII, so callers can use it as it is.
inline bool textIsAscii(const char* s) {
  for (; *s; s++)
    if ((uint8_t)*s >= 0x80) return false;
  return true;
}

// Fold the NUL-terminated UTF-8 string `in` into `out` (cap bytes, always NUL-terminated).
// The result is never longer than the input, so cap = strlen(in) + 1 holds all of it.
// Returns the length written.
inline size_t foldText(const char* in, char* out, size_t cap) {
  if (!cap) return 0;
  const uint8_t* s = (const uint8_t*)in;
  const uint8_t* e = s;
  while (*e) e++;
  size_t o = 0;
  bool lastQ = false;   // a run of unknown characters gives a single "?"
  auto put = [&](const char* r, size_t n) {
    for (size_t i = 0; i < n && o + 1 < cap; i++) out[o++] = r[i];
  };
  while (s < e) {
    uint32_t cp = 0;
    const int n = textfold::decode(s, e, &cp);
    if (n == 0) {   // broken byte: skip just that byte
      if (!lastQ) put("?", 1);
      lastQ = true;
      s++;
      continue;
    }
    bool keep;
    const char* r = textfold::mapOne(cp, &keep);
    if (keep) {
      put((const char*)s, n);
      lastQ = false;
    } else if (!r) {
      if (!lastQ) put("?", 1);
      lastQ = true;
    } else {
      size_t l = 0;
      while (r[l]) l++;
      put(r, l);
      if (l) lastQ = false;
    }
    s += n;
  }
  out[o] = 0;
  return o;
}
