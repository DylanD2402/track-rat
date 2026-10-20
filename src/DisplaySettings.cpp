/**
 * Track Rat - Settings and first-boot screens
 *
 * Part of DisplayManager, in its own file like DisplayPerf.cpp. Settings is a
 * touch list (it is used in the pits; where things are is in SettingsFlow.h)
 * that can also be done with the BOOT button alone. The first-boot questions
 * work with gloves: two big halves of the screen.
 */

#include "DisplayManager.h"
#include "Config.h"
#include "UiTheme.h"

using namespace ui;
using Font = DisplayManager::Font;

void DisplayManager::setRotation(uint8_t r) {
  if (r == rotation) return;
  rotation = r;
  tft.setRotation(r);
  shownValid = false;  // the whole panel is now the other way round: send the next frame whole
}

// Rows y..y+h-1 of the frame, turned through 180 degrees, so text drawn there
// reads the right way for someone holding the screen upside down.
void DisplayManager::flipBand(int y, int h) {
  if (!useSprite) return;
  uint16_t* buf = (uint16_t*)spr.getPointer();
  uint16_t* a = buf + y * W;
  uint16_t* b = buf + (y + h) * W - 1;
  while (a < b) {
    uint16_t t = *a;
    *a++ = *b;
    *b-- = t;
  }
}

// ---------------------------------------------------------------------------
// Settings: a scrolling list like the phone's, and a page for the row you open

using UI = SettingsScreenState;

// An iOS-style switch, right edge at `right`, centred on cy
static void drawSwitch(TFT_eSPI* g, int right, int cy, bool on) {
  const int w = 42, h = 26, x = right - w;
  g->fillRoundRect(x, cy - h / 2, w, h, h / 2, on ? C_GREEN : C_LINE);
  g->fillCircle(on ? x + w - h / 2 : x + h / 2, cy, h / 2 - 3, C_TEXT);
}

// The ">" that says a row opens a page, with its right edge at `right`
static void drawChevron(TFT_eSPI* g, int right, int cy, uint16_t c) {
  for (int d = 0; d < 2; d++) {
    g->drawLine(right - 7 - d, cy - 6, right - 1 - d, cy, c);
    g->drawLine(right - 1 - d, cy, right - 7 - d, cy + 6, c);
  }
}

// A tick, centred on cx, cy (the fonts have no check mark)
static void drawCheck(TFT_eSPI* g, int cx, int cy, uint16_t c) {
  for (int d = 0; d < 2; d++) {
    g->drawLine(cx - 8, cy + d, cx - 3, cy + 5 + d, c);
    g->drawLine(cx - 3, cy + 5 + d, cx + 8, cy - 6 + d, c);
  }
}

// The row BOOT is on: raised, with a fine outline, inside its card
static void rowHighlight(TFT_eSPI* g, int ry) {
  g->fillRoundRect(UI::CARD_X + 3, ry + 2, UI::CARD_W - 6, UI::ROW_H - 4, CORNER - 3, C_RAISED);
  g->drawRoundRect(UI::CARD_X + 3, ry + 2, UI::CARD_W - 6, UI::ROW_H - 4, CORNER - 3, C_TEXT3);
}

// A note in the small grey type, broken into lines (at most three) that fit maxW
void DisplayManager::wrappedCaption(const char* note, int x, int y, int maxW, uint16_t color, bool center) {
  String rest = note;
  for (int line = 0; line < 3 && rest.length(); line++) {
    String cut = rest;
    if (textWidth(rest, Font::Caption) > maxW) {
      int at = -1;
      for (int i = rest.length() - 1; i > 0 && at < 0; i--)
        if (rest[i] == ' ' && textWidth(rest.substring(0, i), Font::Caption) <= maxW) at = i;
      if (at < 0) {
        cut = fit(rest, Font::Caption, maxW);
        rest = "";
      } else {
        cut = rest.substring(0, at);
        rest = rest.substring(at + 1);
      }
    } else {
      rest = "";
    }
    text(cut, x, y + line * 15, Font::Caption, color, center ? TC_DATUM : TL_DATUM);
  }
}

// The line at the bottom when there is no touch
void DisplayManager::settingsHint(const char* line) {
  gfx->fillRect(0, UI::SCREEN_H - UI::HINT_H, W, UI::HINT_H, C_BG);
  gfx->drawFastHLine(0, UI::SCREEN_H - UI::HINT_H, W, C_LINE);
  text(line, W / 2, UI::SCREEN_H - UI::HINT_H / 2, Font::Caption, C_TEXT3, MC_DATUM);
}

void DisplayManager::settingsList(const SettingsData& s, const SettingsScreenState& ui, bool touchOk) {
  char buf[48];
  for (int sec = 0; sec < UI::SECTIONS; sec++) {
    const UI::Section& sc = UI::section(sec);
    const int cardY = ui.sectionScreenY(sec) + UI::captionH(sec);
    const int cardH = sc.count * UI::ROW_H;
    if (cardY + cardH + UI::FOOT_H <= UI::TOP || cardY - UI::CAPTION_H >= ui.viewBottom) continue;  // off screen

    if (sc.title) text(sc.title, UI::CARD_X + 14, cardY - 20, Font::Caption, C_TEXT2);
    gfx->fillRoundRect(UI::CARD_X, cardY, UI::CARD_W, cardH, CORNER, C_SURFACE);

    for (int k = 0; k < sc.count; k++) {
      const int r = sc.first + k, ry = cardY + k * UI::ROW_H, cy = ry + UI::ROW_H / 2;
      const UI::Row& rw = UI::row(r);
      const bool hl = ui.showHighlight && ui.highlight == r;
      if (hl) rowHighlight(gfx, ry);
      else if (k > 0 && !(ui.showHighlight && ui.highlight == r - 1))
        gfx->drawFastHLine(UI::CARD_X + 14, ry, UI::CARD_W - 14, C_LINE);  // inset from the left, like the phone's
      if (cy < 0 || cy >= UI::SCREEN_H) continue;

      const int right = UI::CARD_X + UI::CARD_W - 14;
      if (rw.kind == UI::RowKind::Back) {
        text("Back to menu", UI::CARD_X + UI::CARD_W / 2, cy, Font::Body, C_TEXT, MC_DATUM);
        continue;
      }
      const int nameW = text(UI::rowLabel(rw.item), UI::CARD_X + 14, cy, Font::Body, C_TEXT, ML_DATUM);
      if (rw.kind == UI::RowKind::Toggle) {
        drawSwitch(gfx, right, cy, UI::toggleOn(s, rw.item));
      } else {
        drawChevron(gfx, right, cy, C_TEXT3);
        settings::valueText(s, rw.item, buf, sizeof(buf));
        const int valueRight = right - 18;
        text(fit(buf, Font::Body, valueRight - (UI::CARD_X + 14 + nameW + 10)), valueRight, cy, Font::Body, C_TEXT2, MR_DATUM);
      }
    }
    if (sc.foot) wrappedCaption(sc.foot, UI::CARD_X + 14, cardY + cardH + 6, UI::CARD_W - 28, C_TEXT2, false);
  }

  // The top bar stays put: rows scroll under it
  gfx->fillRect(0, 0, W, UI::TOP, C_BG);
  header("‹  Menu", nullptr);
  text("Settings", W / 2, HEADER_H / 2, Font::Button, C_TEXT, MC_DATUM);

  int by, bh;
  if (ui.scrollBar(by, bh)) gfx->fillRoundRect(W - 5, by, 3, bh, 1, C_TEXT3);
  if (!touchOk) settingsHint("BOOT: press for next row, hold to open");
}

// Speed units, Big number, Compare with, GPS mode: the options in a card, the current one ticked
void DisplayManager::settingsChoicePage(const SettingsData& s, const SettingsScreenState& ui, bool touchOk) {
  char buf[48];
  const int n = UI::choiceCount(ui.item);
  gfx->fillRoundRect(UI::CARD_X, UI::CHOICE_TOP, UI::CARD_W, n * UI::ROW_H, CORNER, C_SURFACE);
  for (int i = 0; i < n; i++) {
    const int ry = ui.choiceRowY(i), cy = ry + UI::ROW_H / 2;
    const bool hl = ui.showHighlight && ui.choiceHl == i;
    if (hl) rowHighlight(gfx, ry);
    else if (i > 0 && !(ui.showHighlight && ui.choiceHl == i - 1))
      gfx->drawFastHLine(UI::CARD_X + 14, ry, UI::CARD_W - 14, C_LINE);
    SettingsData option = s;
    UI::choiceSet(option, ui.item, i);
    settings::valueText(option, ui.item, buf, sizeof(buf));
    text(fit(buf, Font::Body, UI::CARD_W - 60), UI::CARD_X + 14, cy, Font::Body, C_TEXT, ML_DATUM);
    if (UI::choiceIndex(s, ui.item) == i) drawCheck(gfx, UI::CARD_X + UI::CARD_W - 24, cy, C_TEXT);
  }
  wrappedCaption(settings::itemNote(ui.item), UI::CARD_X + 14, UI::CHOICE_TOP + n * UI::ROW_H + 8, UI::CARD_W - 28, C_TEXT2, false);

  header("‹  Settings", nullptr);
  text(UI::rowLabel(ui.item), W / 2, HEADER_H / 2, Font::Button, C_TEXT, MC_DATUM);
  if (!touchOk) settingsHint("BOOT: press for next, hold to choose");
}

// Brightness and Time zone: the value big, and - and + (and a bar to touch, for brightness)
void DisplayManager::settingsLevelPage(const SettingsData& s, const SettingsScreenState& ui, bool touchOk, int64_t localMs) {
  char buf[48];
  const bool bright = ui.page == UI::Page::Brightness;
  const int vy = bright ? 76 : 64;
  settings::valueText(s, ui.item, buf, sizeof(buf));
  if (bright) {
    text(buf, W / 2, vy, Font::NumL, C_TEXT, MC_DATUM);
    const int fill = UI::BAR_W * s.brightness / 100;
    gfx->fillRoundRect(UI::BAR_X, UI::BAR_Y, UI::BAR_W, UI::BAR_H, UI::BAR_H / 2, C_LINE);
    gfx->fillRoundRect(UI::BAR_X, UI::BAR_Y, fill, UI::BAR_H, UI::BAR_H / 2, C_TEXT);
    gfx->fillCircle(UI::BAR_X + fill, UI::BAR_Y + UI::BAR_H / 2, 11, C_TEXT);
  } else {
    // "UTC" in grey, then +5:30 big
    const String num = String(buf + 3);
    const int wUtc = textWidth("UTC", Font::Title), wNum = textWidth(num, Font::NumL);
    const int x0 = (W - (wUtc + 8 + wNum)) / 2;
    text("UTC", x0, vy + 4, Font::Title, C_TEXT2, ML_DATUM);
    text(num, x0 + wUtc + 8, vy, Font::NumL, C_TEXT, ML_DATUM);
    if (localMs >= 0) {
      uint32_t m = (uint32_t)(localMs / 60000);
      snprintf(buf, sizeof(buf), "Local time now  %02lu:%02lu", (unsigned long)(m / 60 % 24), (unsigned long)(m % 60));
      text(buf, W / 2, 100, Font::Body, C_TEXT, MC_DATUM);
    }
    wrappedCaption(settings::itemNote(ui.item), W / 2, 116, W - 40, C_TEXT2, true);
  }

  if (touchOk) {
    const bool atMin = bright ? s.brightness <= settings::BRIGHTNESS_MIN : s.utcOffsetMin <= settings::UTC_MIN;
    const bool atMax = bright ? s.brightness >= settings::BRIGHTNESS_MAX : s.utcOffsetMin >= settings::UTC_MAX;
    gfx->fillRoundRect(UI::MINUS_X, UI::BTN_Y, UI::BTN_W, UI::BTN_H, CORNER, C_SURFACE);
    gfx->fillRoundRect(UI::PLUS_X, UI::BTN_Y, UI::BTN_W, UI::BTN_H, CORNER, C_SURFACE);
    text("−", UI::MINUS_X + UI::BTN_W / 2, UI::BTN_Y + UI::BTN_H / 2, Font::NumL, atMin ? C_TEXT3 : C_TEXT, MC_DATUM);
    text("+", UI::PLUS_X + UI::BTN_W / 2, UI::BTN_Y + UI::BTN_H / 2, Font::NumL, atMax ? C_TEXT3 : C_TEXT, MC_DATUM);
  }

  header("‹  Settings", nullptr);
  text(UI::rowLabel(ui.item), W / 2, HEADER_H / 2, Font::Button, C_TEXT, MC_DATUM);
  if (!touchOk) settingsHint("BOOT: press for +, hold to go back");
}

void DisplayManager::drawSettings(const SettingsData& s, const SettingsScreenState& ui, bool touchOk, int64_t localMs) {
  beginFrame();
  if (ui.page == UI::Page::List) settingsList(s, ui, touchOk);
  else if (ui.page == UI::Page::Choice) settingsChoicePage(s, ui, touchOk);
  else settingsLevelPage(s, ui, touchOk, localMs);
  endFrame();
}

// A solid arrow, 74 px tall, centred on cx, cy.
static void arrow(TFT_eSPI* g, int cx, int cy, bool up, uint16_t c) {
  const int headH = 34, headW = 64, shaftW = 20, shaftH = 40, top = cy - (headH + shaftH) / 2;
  for (int i = 0; i < headH; i++) {
    int w = 4 + (headW - 4) * i / (headH - 1);
    g->fillRect(cx - w / 2, up ? top + i : top + headH + shaftH - 1 - i, w, 1, c);
  }
  g->fillRect(cx - shaftW / 2, up ? top + headH : top, shaftW, shaftH, c);
}

void DisplayManager::drawSetup(int step, int highlight, bool touchOk) {
  beginFrame();
  // Two choices, in the left and right thirds of the screen (where the taps are)
  const int bw = W / 3 - 8, by = 56, bh = 136;
  const int bx[2] = {4, W - 4 - bw};
  for (int i = 0; i < 2; i++) {
    bool hl = i == highlight && !touchOk;
    gfx->fillRoundRect(bx[i], by, bw, bh, CORNER + 2, hl ? C_RAISED : C_SURFACE);
    if (hl) gfx->drawRoundRect(bx[i], by, bw, bh, CORNER + 2, C_TEXT);
  }

  if (step == 0) {
    // Which way up? The screen may be mounted either way, so the question is
    // written at the top and again, upside down, at the bottom; the two
    // arrows point opposite ways, so one points up whichever way it's held.
    const char* ask = "Tap the arrow that points up";
    const char* hint = touchOk ? "" : "BOOT: press for the other, hold to choose";
    for (int band = 0; band < 2; band++) {
      int y = band == 0 ? 0 : H - 46;
      centered(ask, y + 6, Font::Title, C_TEXT);
      if (hint[0]) centered(hint, y + 32, Font::Caption, C_TEXT2);
      if (band == 1) flipBand(y, 46);
    }
    arrow(gfx, bx[0] + bw / 2, by + bh / 2, true, C_TEXT);
    arrow(gfx, bx[1] + bw / 2, by + bh / 2, false, C_TEXT);
  } else {
    centered("Speed units", 10, Font::Title, C_TEXT);
    if (touchOk) centered("Tap the one you want", 38, Font::Body, C_TEXT2);
    else centered("BOOT: press for the other, hold to choose", 40, Font::Caption, C_TEXT2);
    const char* name[2] = {"km/h", "mph"};
    const char* example[2] = {"100 km/h", "62 mph"};
    for (int i = 0; i < 2; i++) {
      text(name[i], bx[i] + bw / 2, by + bh / 2 - 8, Font::Title, C_TEXT, MC_DATUM);
      text(example[i], bx[i] + bw / 2, by + bh / 2 + 22, Font::Caption, C_TEXT3, MC_DATUM);
    }
    text("or", W / 2, by + bh / 2, Font::Body, C_TEXT3, MC_DATUM);
    centered("Change these any time in Settings", 210, Font::Caption, C_TEXT3);
  }
  endFrame();
}
