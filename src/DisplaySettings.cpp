/**
 * Track Rat - Settings and first-boot screens
 *
 * Part of DisplayManager, in its own file like DisplayPerf.cpp. Both screens
 * work with gloves: the left and right thirds and the centre are the only
 * touch targets, and everything can be done with the BOOT button alone.
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

void DisplayManager::drawSettings(const SettingsData& s, int item, bool touchOk, int64_t localMs) {
  beginFrame();
  header("‹  Settings", nullptr);  // the top bar goes back
  char buf[48];

  if (item >= SETTING_ITEM_COUNT) {  // the "done" page
    centered("All set", 64, Font::Title, C_TEXT);
    centered("Changes are saved as you make them", 100, Font::Body, C_TEXT2);
    if (touchOk) {
      footerButtons("‹", "Done", "›");
      centered("Top bar or hold: back to the menu", 170, Font::Caption, C_TEXT3);
    } else {
      centered("BOOT: hold for the menu, press to start again", 212, Font::Body, C_TEXT3);
    }
    endFrame();
    return;
  }

  snprintf(buf, sizeof(buf), "%d of %d", item + 1, SETTING_ITEM_COUNT);
  text(buf, W - 14, HEADER_H + 12, Font::Caption, C_TEXT3, TR_DATUM);  // below the status icons in the top bar

  const SettingItem it = (SettingItem)item;
  label(settings::itemName(it), W / 2, HEADER_H + 12, TC_DATUM);

  // The value, with the - and + that the left and right thirds of the screen press
  const int cy = 62, ch = 66;
  gfx->fillRoundRect(8, cy, W - 16, ch, CORNER + 2, C_SURFACE);
  if (touchOk) {
    text("−", 30, cy + ch / 2, Font::Title, C_TEXT2, MC_DATUM);
    text("+", W - 30, cy + ch / 2, Font::Title, C_TEXT2, MC_DATUM);
  }
  settings::valueText(s, it, buf, sizeof(buf));
  const Font vf = textWidth(buf, Font::Title) <= W - 100 ? Font::Title : Font::Body;  // long values: smaller font
  text(fit(buf, vf, W - 100), W / 2, cy + ch / 2 + (it == SettingItem::Brightness ? -6 : 1), vf, C_TEXT, MC_DATUM);
  if (it == SettingItem::Brightness) {  // a level bar under the percentage
    const int bx = 80, bw = W - 160, by = cy + ch - 18;
    gfx->fillRoundRect(bx, by, bw, 6, 3, C_LINE);
    gfx->fillRoundRect(bx, by, max(6, bw * s.brightness / 100), 6, 3, C_TEXT);
  }

  // The note, on two lines if it doesn't fit on one
  String note = settings::itemNote(it);
  int cut = -1;
  if (textWidth(note, Font::Caption) > W - 24)
    for (int i = note.length() - 1; i > 0 && cut < 0; i--)
      if (note[i] == ' ' && textWidth(note.substring(0, i), Font::Caption) <= W - 24) cut = i;
  if (cut < 0) {
    centered(note, 140, Font::Caption, C_TEXT2);
  } else {
    centered(note.substring(0, cut), 140, Font::Caption, C_TEXT2);
    centered(note.substring(cut + 1), 156, Font::Caption, C_TEXT2);
  }
  if (it == SettingItem::TimeZone && localMs >= 0) {
    uint32_t m = (uint32_t)(localMs / 60000);
    snprintf(buf, sizeof(buf), "Local time now  %02lu:%02lu", (unsigned long)(m / 60 % 24), (unsigned long)(m % 60));
    centered(buf, 174, Font::Body, C_TEXT);
  }

  if (touchOk) footerButtons("−", "Next", "+");
  else centered("BOOT: press for next, hold to change", 212, Font::Body, C_TEXT3);
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
