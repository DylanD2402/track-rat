/**
 * Track Rat - the Adjust start / Adjust finish screen (9.3b)
 *
 * Part of DisplayManager. What it shows (the runs' paths near the line, the old and new times) is
 * worked out in AdjustLine.h and passed in as an adjust::View; this only lays it out.
 */

#include "DisplayManager.h"
#include <vector>
#include "Config.h"
#include "UiTheme.h"

using namespace ui;
using Font = DisplayManager::Font;

// The part of the line (x0, y0)-(x1, y1) inside the box, if any (Liang-Barsky)
static bool clipToBox(float& x0, float& y0, float& x1, float& y1, float minX, float minY, float maxX, float maxY) {
  float t0 = 0, t1 = 1;
  const float dx = x1 - x0, dy = y1 - y0;
  const float p[4] = {-dx, dx, -dy, dy};
  const float q[4] = {x0 - minX, maxX - x0, y0 - minY, maxY - y0};
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0) {
      if (q[i] < 0) return false;
    } else {
      const float t = q[i] / p[i];
      if (p[i] < 0) {
        if (t > t1) return false;
        if (t > t0) t0 = t;
      } else {
        if (t < t0) return false;
        if (t < t1) t1 = t;
      }
    }
  }
  const float nx0 = x0 + t0 * dx, ny0 = y0 + t0 * dy, nx1 = x0 + t1 * dx, ny1 = y0 + t1 * dy;
  x0 = nx0;
  y0 = ny0;
  x1 = nx1;
  y1 = ny1;
  return true;
}

void DisplayManager::drawAdjust(const adjust::View& v, int highlight, bool touchOk) {
  using namespace adjust;
  beginFrame();
  header(v.finish ? "Adjust finish" : "Adjust start", nullptr);

  auto button = [&](int x, int y, int w, int h, const char* caption, bool enabled, bool primary, bool hl) {
    gfx->fillRoundRect(x, y, w, h, CORNER, enabled && primary ? C_TEXT : hl ? C_RAISED : C_SURFACE);
    if (hl) gfx->drawRoundRect(x, y, w, h, CORNER, C_BRAND);
    text(fit(caption, Font::Body, w - 8), x + w / 2, y + h / 2 + 1, Font::Body, !enabled ? C_TEXT3 : primary ? C_BG : C_TEXT, MC_DATUM);
  };
  // Words on lines no wider than maxW
  auto wrap = [&](const std::string& s, Font f, int maxW) {
    std::vector<String> lines;
    String cur;
    size_t i = 0;
    while (i < s.size()) {
      size_t j = s.find(' ', i);
      if (j == std::string::npos) j = s.size();
      const String word = s.substr(i, j - i).c_str();
      const String tryLine = cur.length() ? cur + " " + word : word;
      if (cur.length() && textWidth(tryLine, f) > maxW) {
        lines.push_back(cur);
        cur = word;
      } else {
        cur = tryLine;
      }
      i = j + 1;
    }
    if (cur.length()) lines.push_back(cur);
    return lines;
  };

  if (v.phase != Phase::Ready) {  // reading the logs, nothing to adjust, or timing the runs again
    gfx->fillRoundRect(8, HEADER_H + 10, W - 16, 110, CORNER + 2, C_SURFACE);
    const char* title = v.phase == Phase::Reading ? "Reading runs…" : v.phase == Phase::Saving ? "Re-timing…"
                        : v.phase == Phase::SaveFailed ? "Saved, not re-timed" : "Nothing to adjust";
    centered(title, HEADER_H + 22, Font::Title, v.phase == Phase::NoLogs || v.phase == Phase::SaveFailed ? C_YELLOW : C_TEXT);
    int y = HEADER_H + 60;
    for (const String& l : wrap(v.message, Font::Body, W - 48)) {
      centered(l, y, Font::Body, C_TEXT2);
      y += 19;
    }
    if ((v.phase == Phase::Reading || v.phase == Phase::Saving) && v.total > 0) {  // how far along
      const int bx = 40, bw = W - 80, by = HEADER_H + 100;
      gfx->fillRoundRect(bx, by, bw, 6, 3, C_RAISED);
      const int fill = bw * (v.done > v.total ? v.total : v.done) / v.total;
      if (fill > 5) gfx->fillRoundRect(bx, by, fill, 6, 3, C_BRAND);
    }
    const char* back = v.phase == Phase::Saving ? "Back to the hub" : v.phase == Phase::Reading ? "Cancel" : "Back";  // (Back: the hub)
    button(5, ROW_B_Y, W - 10, ROW_B_H, back, true, false, highlight == BTN_CANCEL);
    endFrame();
    return;
  }

  // --- the paths near the line, travelling left to right ---
  const int px = 6, py = HEADER_H + 3, pw = W - 12, ph = 52;
  gfx->fillRoundRect(px, py, pw, ph, CORNER, C_SURFACE);
  const float scale = pw / (2 * (float)VIEW_HALF_M);  // pixels a metre
  const float cx = px + pw / 2.0f, cy = py + ph / 2.0f;
  const float minX = px + 2, maxX = px + pw - 3, minY = py + 2, maxY = py + ph - 3;
  for (int k = -3; k <= 3; k++) {  // a mark every 5 m
    const int tx = (int)(cx + k * 5 * scale);
    gfx->drawFastVLine(tx, py + ph - 6, 4, C_LINE);
  }
  if (v.offsetM != 0) {  // where the line was
    for (int yy = (int)minY; yy < (int)maxY; yy += 4) gfx->drawFastVLine((int)cx, yy, 2, C_TEXT3);
  }
  int bestIdx = -1;
  for (size_t i = 0; i < v.rows.size(); i++)
    if (v.rows[i].best) bestIdx = (int)i;
  for (int pass = 0; pass < 2; pass++) {  // the best last, on top
    for (size_t i = 0; i < v.paths.size(); i++) {
      if (((int)i == bestIdx) != (pass == 1)) continue;
      const std::vector<PathPt>& pts = v.paths[i];
      for (size_t k = 1; k < pts.size(); k++) {
        if (fabsf(pts[k].along - pts[k - 1].along) > 6 || fabsf(pts[k].across - pts[k - 1].across) > 6) continue;  // a gap
        float x0 = cx + pts[k - 1].along * scale, y0 = cy - pts[k - 1].across * scale;
        float x1 = cx + pts[k].along * scale, y1 = cy - pts[k].across * scale;
        if (clipToBox(x0, y0, x1, y1, minX, minY, maxX, maxY)) gfx->drawLine((int)x0, (int)y0, (int)x1, (int)y1, pass ? C_GREEN : C_TEXT2);
      }
    }
  }
  // The line as it is now, with an arrow for the way the cars go
  {
    float lx = cx + v.offsetM * scale;
    if (lx < minX) lx = minX;
    if (lx > maxX) lx = maxX;
    const int x = (int)lx;
    const uint16_t c = v.offsetM != 0 ? C_YELLOW : C_TEXT;
    gfx->fillRect(x - 1, py + 3, 2, ph - 6, c);
    gfx->drawLine(x - 4, py + 4, x + 1, py + 8, c);
    gfx->drawLine(x - 4, py + 12, x + 1, py + 8, c);
  }
  if (!touchOk) label("BOOT: hold picks, press uses", px + 8, py + 4);

  // --- how far it has moved, and the best ---
  int y = py + ph + 4;
  text(v.offsetText.c_str(), px + 2, y, Font::Caption, v.offsetM != 0 ? C_YELLOW : v.canMove ? C_TEXT2 : C_RED);
  if (v.summary.length()) text(fit(v.summary.c_str(), Font::Caption, 190), W - 8, y, Font::Caption, C_TEXT2, TR_DATUM);
  y += 15;

  // --- standing starts ---
  if (v.note.length()) {
    for (const String& l : wrap(v.note, Font::Caption, W - 16)) {
      label(l.c_str(), 8, y);
      y += 13;
    }
    y += 2;
  }

  // --- the runs: number, old time, new time, change; two columns, the latest that fit ---
  const int rowH = 13, listBottom = ROW_A_Y - 3;
  const int rowsFit = (listBottom - y) / rowH > 0 ? (listBottom - y) / rowH : 1;
  const int n = (int)v.rows.size(), cap = 2 * rowsFit, first = n > cap ? n - cap : 0;
  if (n == 0) label("No runs", 8, y);
  for (int i = first; i < n; i++) {
    const ViewRow& r = v.rows[i];
    const int k = i - first, col = k / rowsFit, x = 10 + col * 156, yy = y + (k % rowsFit) * rowH;
    const int nw = text(String(r.number), x, yy, Font::Caption, C_TEXT3);
    (void)nw;
    text(r.oldText.c_str(), x + 18, yy, Font::Caption, r.changed ? C_TEXT3 : C_TEXT2);
    if (r.changed) text("›", x + 62, yy, Font::Caption, C_TEXT3);
    const uint16_t nc = r.best ? C_GREEN : r.changed ? C_YELLOW : C_TEXT;
    const int w = r.changed ? text(r.newText.c_str(), x + 72, yy, Font::Caption, nc) : 0;
    if (r.diffText.length()) text(r.diffText.c_str(), x + 72 + w + 5, yy, Font::Caption, C_TEXT2);
    if (r.best) gfx->fillCircle(x - 5, yy + 7, 2, C_GREEN);
  }
  if (first > 0) label(("+" + String(first) + " earlier").c_str(), W - 8, listBottom - 10, TR_DATUM);

  // --- buttons ---
  const bool early = v.canMove && !v.atEarliest, late = v.canMove && !v.atLatest;
  button(5, ROW_A_Y, 118, ROW_A_H, "‹ 0.5 m earlier", early, false, highlight == BTN_EARLIER);
  button(128, ROW_A_Y, 64, ROW_A_H, "Reset", v.offsetM != 0, false, highlight == BTN_RESET);
  button(197, ROW_A_Y, 118, ROW_A_H, "0.5 m later ›", late, false, highlight == BTN_LATER);
  button(5, ROW_B_Y, 150, ROW_B_H, "Cancel", true, false, highlight == BTN_CANCEL);
  button(165, ROW_B_Y, 150, ROW_B_H, "Save", true, true, highlight == BTN_SAVE);
  endFrame();
}
