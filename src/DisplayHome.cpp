/**
 * Track Rat - the home screen and the Track day list's extra entries (9.1)
 *
 * Part of DisplayManager, in its own file so the other screens stay as they are. What the Next up card
 * says is chosen in NextUp.h; where things are, for the taps too, is in HomeLayout.h.
 */

#include <math.h>
#include "DisplayManager.h"
#include "Config.h"
#include "HomeLayout.h"
#include "Settings.h"
#include "UiTheme.h"

using namespace ui;
using Font = DisplayManager::Font;

// Small line icons for the home tiles, centred on cx, cy (about 32 x 28)
enum class TileIcon { Flag, Cones, Gauge, Route };

static void tileIcon(TFT_eSPI* g, TileIcon which, int cx, int cy, uint16_t c) {
  if (which == TileIcon::Route) {  // Drive: a winding route ending in a dot
    const int pts[][2] = {{-14, 9}, {-8, 1}, {-2, 5}, {4, -3}, {10, -1}, {14, -9}};
    for (int i = 1; i < 6; i++)
      for (int t = 0; t < 2; t++) g->drawLine(cx + pts[i - 1][0], cy + pts[i - 1][1] + t, cx + pts[i][0], cy + pts[i][1] + t, c);
    g->fillCircle(cx + 14, cy - 9, 3, c);
    g->fillCircle(cx - 14, cy + 9, 2, c);
  } else if (which == TileIcon::Flag) {  // Track day: a chequered flag
    g->fillRect(cx - 12, cy - 13, 2, 27, c);
    for (int r = 0; r < 3; r++)
      for (int k = 0; k < 4; k++)
        if ((r + k) % 2 == 0) g->fillRect(cx - 10 + k * 6, cy - 13 + r * 6, 6, 6, c);
    g->drawRect(cx - 10, cy - 13, 24, 18, c);
  } else if (which == TileIcon::Cones) {  // Autocross: three cones, a slalom line weaving through
    for (int i = 0; i < 3; i++) {
      const int x = cx - 13 + i * 13, base = cy + 12;
      for (int row = 0; row < 14; row++) g->drawFastHLine(x - 1 - row / 4, base - row, 3 + row / 2, c);  // apex up
      g->fillRect(x - 6, base, 13, 2, c);
    }
    for (int i = 0; i < 2; i++) {  // the line, over the top of the cones
      g->drawLine(cx - 15 + i * 14, cy - 8 + (i ? 4 : 0), cx - 1 + i * 14, cy - 8 + (i ? 0 : 4), c);
    }
  } else {  // Drag: a gauge, its needle up and to the right
    for (int a = 180; a <= 360; a += 6) {
      const float rad = a * 0.0174533f;
      g->fillRect(cx + (int)lroundf(cosf(rad) * 15) - 1, cy + 8 + (int)lroundf(sinf(rad) * 15) - 1, 2, 2, c);
    }
    g->drawLine(cx, cy + 8, cx + 9, cy - 4, c);
    g->drawLine(cx + 1, cy + 8, cx + 10, cy - 4, c);
    g->fillCircle(cx, cy + 8, 3, c);
    g->drawFastHLine(cx - 15, cy + 12, 31, c);
  }
}

// A gear: a ring with eight teeth
static void gearIcon(TFT_eSPI* g, int cx, int cy, uint16_t c, uint16_t hole) {
  for (int i = 0; i < 8; i++) {
    const float rad = i * 0.785398f;
    g->fillRect(cx + (int)lroundf(cosf(rad) * 9) - 2, cy + (int)lroundf(sinf(rad) * 9) - 2, 5, 5, c);
  }
  g->fillCircle(cx, cy, 8, c);
  g->fillCircle(cx, cy, 3, hole);
}

void DisplayManager::drawHome(const nextup::Choice& next, int highlight, const GPSHandler& gps, bool touchOk) {
  beginFrame();
  header("Menu", &gps);

  // --- Next up: one tap does the most likely thing ---
  const bool waiting = next.kind == nextup::Kind::NoFix;
  const int cx = 6, cw = W - 12, cy = home::CARD_Y, ch = home::CARD_H;
  const bool cardHl = highlight == home::NextUp;
  if (cardHl) {  // BOOT: a ring round the card
    gfx->drawRoundRect(cx - 3, cy - 3, cw + 6, ch + 6, CORNER + 3, C_GREEN);
    gfx->drawRoundRect(cx - 2, cy - 2, cw + 4, ch + 4, CORNER + 2, C_GREEN);
  }
  const uint16_t cardBg = waiting ? C_SURFACE : C_TEXT;
  const uint16_t ink = waiting ? C_YELLOW : C_BG;                  // the title
  const uint16_t inkSoft = waiting ? C_TEXT2 : rgb(70, 70, 74);    // the rest
  gfx->fillRoundRect(cx, cy, cw, ch, CORNER, cardBg);
  text("NEXT UP", cx + 14, cy + 6, Font::Caption, waiting ? C_TEXT3 : rgb(110, 110, 114), TL_DATUM);
  const int arrowW = waiting ? 0 : 34;  // room for the play arrow
  const int textW = cw - 28 - arrowW;
  const String title = next.title.c_str();
  const Font tf = textWidth(title, Font::Title) <= textW ? Font::Title : Font::Button;
  text(fit(title, tf, textW), cx + 14, cy + (tf == Font::Title ? 19 : 22), tf, ink, TL_DATUM);
  text(fit(next.detail.c_str(), Font::Body, textW), cx + 14, cy + 49, Font::Body, inkSoft, TL_DATUM);
  if (!waiting) {  // a play arrow: this starts something
    const int ax = cx + cw - 38, ay = cy + ch / 2;
    for (int col = 0; col < 18; col++) {
      const int half = 14 - col * 14 / 18;
      gfx->drawFastVLine(ax + col, ay - half, half * 2 + 1, C_BG);
    }
  }

  // --- the four tiles ---
  struct Tile { const char* title; TileIcon icon; };
  const Tile tiles[4] = {
      {"Track day", TileIcon::Flag}, {"Autocross", TileIcon::Cones}, {"Drag", TileIcon::Gauge}, {"Drive", TileIcon::Route}};
  const int quarter = W / 4, ty = home::TILE_Y, th = home::TILE_H;
  for (int i = 0; i < 4; i++) {
    const int x = i * quarter + 3, w = quarter - 6, mid = x + w / 2;
    const bool hl = highlight == home::TrackDay + i;
    const bool needsFix = i == 3 && !gps.hasFix();  // Drive is Free Roam, which needs a fix
    gfx->fillRoundRect(x, ty, w, th, CORNER + 2, hl ? C_RAISED : C_SURFACE);
    if (hl) gfx->drawRoundRect(x, ty, w, th, CORNER + 2, C_TEXT);
    tileIcon(gfx, tiles[i].icon, mid, ty + 24, needsFix ? C_TEXT3 : C_TEXT);
    text(tiles[i].title, mid, ty + 53, Font::Body, needsFix ? C_TEXT2 : C_TEXT, MC_DATUM);
  }

  // --- Wi-Fi and Settings, side by side ---
  const int ry = home::ROW_Y, rh = home::ROW_H, half = W / 2;
  wifiButton(ry, rh, highlight == home::Wifi, false, 4, half - 8);
  {
    const int x = half + 4, w = half - 8;
    const bool hl = highlight == home::Settings;
    gfx->fillRoundRect(x, ry, w, rh, CORNER, hl ? C_RAISED : C_SURFACE);
    gfx->drawRoundRect(x, ry, w, rh, CORNER, hl ? C_TEXT : C_LINE);
    const int lw = textWidth("Settings", Font::Button) + 28;
    gearIcon(gfx, x + (w - lw) / 2 + 9, ry + rh / 2, C_TEXT2, hl ? C_RAISED : C_SURFACE);
    text("Settings", x + (w - lw) / 2 + 28, ry + rh / 2 + 1, Font::Button, C_TEXT, ML_DATUM);
  }

  centered(touchOk ? "Top bar: GPS status" : "BOOT: press for next, hold to choose", 226, Font::Caption, C_TEXT3);
  endFrame();
}

void DisplayManager::drawTrackDayExtra(bool back, int index, int count, const GPSHandler& gps, bool touchOk) {
  beginFrame();
  char buf[40];
  if (back) snprintf(buf, sizeof(buf), "‹  Track day");
  else snprintf(buf, sizeof(buf), "‹  Track day   %d of %d", index + 1, count);
  header(buf, &gps);

  if (back) {
    centered("Back to the menu", HEADER_H + 50, Font::Title, C_TEXT);
    centered("BOOT: hold to go back,", 124, Font::Body, C_TEXT2);
    centered("press for the first track", 146, Font::Body, C_TEXT2);
    endFrame();
    return;
  }

  const bool fix = gps.hasFix();
  centered("+ New circuit", HEADER_H + 10, Font::Title, C_GREEN);
  centered("A track that isn't in the list", HEADER_H + 42, Font::Body, C_TEXT2);

  const int sy = 96, sh = 76;
  gfx->fillRoundRect(8, sy, W - 16, sh, CORNER, C_SURFACE);
  text("1", 24, sy + 18, Font::Button, C_GREEN, MC_DATUM);
  text("Start, then drive off", 42, sy + 18, Font::Body, C_TEXT, ML_DATUM);
  text("2", 24, sy + 40, Font::Button, C_GREEN, MC_DATUM);
  text("Tap the screen as you cross the", 42, sy + 40, Font::Body, C_TEXT, ML_DATUM);
  text("start/finish line", 42, sy + 58, Font::Body, C_TEXT, ML_DATUM);

  if (!fix) centered("Needs a GPS fix first", 180, Font::Caption, C_YELLOW);
  if (touchOk) footerButtons("‹", fix ? "Start" : "", "›");
  else centered(fix ? "BOOT: press for next, hold to start" : "BOOT: press for next", 212, Font::Body, C_TEXT3);
  endFrame();
}
