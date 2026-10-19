/**
 * Track Rat - Display implementation
 */

#include "DisplayManager.h"
#include "Config.h"
#include "SessionLog.h"
#include "Storage.h"
#include "RaceBoxBle.h"
#include "Settings.h"
#include "UiTheme.h"
#include "fonts/InterBody.h"
#include "fonts/InterButton.h"
#include "fonts/InterCaption.h"
#include "fonts/InterHero.h"
#include "fonts/InterNumL.h"
#include "fonts/InterNumXL.h"
#include "fonts/InterTitle.h"

using namespace ui;
using Font = DisplayManager::Font;

static const uint8_t* const FONT_DATA[] = {InterCaption, InterBody,  InterButton, InterTitle,
                                           InterHero,    InterNumL, InterNumXL};

const uint8_t* DisplayManager::fontData(Font f) { return FONT_DATA[(int)f]; }

// Smooth fonts blend their edges with the text background colour unless a
// callback says what's underneath. This one reads the frame being drawn, so
// text is smooth on any fill (buttons, the coloured delta box...).
static TFT_eSPI* blendTarget = nullptr;
static uint16_t readBackground(uint16_t x, uint16_t y) {
  if (x >= W || y >= H) return C_BG;
  return blendTarget->readPixel(x, y);
}

bool DisplayManager::begin() {
  tft.init();
  rotation = Settings::get().rotation;  // the Screen setting: normal or upside down
  tft.setRotation(rotation);
  tft.invertDisplay(DISPLAY_INVERT);
  tft.fillScreen(C_BG);
  setBacklight(true);

  spr.setColorDepth(16);
  useSprite = spr.createSprite(W, H) != nullptr;  // ~150 KB, lands in PSRAM
  if (useSprite) {
    // Copy of what is currently on the panel, so endFrame() can send only changed rows
    shown = (uint16_t*)ps_malloc(W * H * sizeof(uint16_t));
    shownValid = false;
  }
  gfx = useSprite ? (TFT_eSPI*)&spr : &tft;
  blendTarget = gfx;
  gfx->setCallback(readBackground);
  DEBUG_PRINTF("Display: %dx%d, %s\n", tft.width(), tft.height(),
               useSprite ? "sprite buffer in PSRAM" : "NO sprite (PSRAM missing?) - direct draw");

#if DISPLAY_TEST_PATTERN
  // Four vertical stripes, each a quarter of the width. If one is missing or
  // shows something else, that part of the panel isn't being written.
  const uint16_t stripe[4] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE};
  const char* names[4] = {"RED", "GREEN", "BLUE", "WHITE"};
  for (int i = 0; i < 4; i++) {
    gfx->fillRect(i * W / 4, 0, W / 4, H, stripe[i]);
    text(names[i], i * W / 4 + W / 8, H / 2, Font::Button, i == 3 ? TFT_BLACK : TFT_WHITE, MC_DATUM);
  }
  gfx->drawRect(0, 0, W, H, TFT_YELLOW);  // outline should touch all 4 edges
  if (useSprite) spr.pushSprite(0, 0);
  DEBUG_PRINTLN("Display: test pattern (red | green | blue | white) for 2 s");
  delay(2000);
#endif
  return true;
}

void DisplayManager::beginFrame() {
  // Not gfx->fillScreen(): TFT_eSprite doesn't override it, and the base
  // version clears the panel's portrait size (240x320), leaving columns
  // 240-319 of the landscape sprite uncleared.
  if (useSprite) spr.fillSprite(C_BG);
  else tft.fillScreen(C_BG);
  gfx->setTextDatum(TL_DATUM);
}

// Sending all 320x240 pixels takes ~50 ms, long enough to see the old and new
// frame at once. Send only bands of rows that differ from what's on screen.
void DisplayManager::endFrame() {
  if (!useSprite) return;
  uint16_t* buf = (uint16_t*)spr.getPointer();
  if (!DISPLAY_PARTIAL_UPDATE || !shown || !shownValid) {
    spr.pushSprite(0, 0);
    if (shown) {
      memcpy(shown, buf, W * H * sizeof(uint16_t));
      shownValid = true;
    }
    return;
  }

  const size_t rowBytes = W * sizeof(uint16_t);
  const int MERGE_GAP = 8;  // join bands separated by only a few unchanged rows
  int bandStart = -1, lastChanged = -1;
  for (int y = 0; y <= H; y++) {
    bool changed = y < H && memcmp(buf + y * W, shown + y * W, rowBytes) != 0;
    if (changed) {
      if (bandStart < 0) bandStart = y;
      lastChanged = y;
    } else if (bandStart >= 0 && (y == H || y - lastChanged > MERGE_GAP)) {
      int h = lastChanged - bandStart + 1;
      spr.pushSprite(0, bandStart, 0, bandStart, W, h);
      memcpy(shown + bandStart * W, buf + bandStart * W, h * rowBytes);
      bandStart = -1;
    }
  }
}

// ---------------------------------------------------------------------------
// Text

// Only one smooth font can be loaded at a time; loading one takes a few
// microseconds (its glyph table is read from flash), so keep the last one.
void DisplayManager::useFont(Font f) {
  if ((int)f == loadedFont) return;
  gfx->loadFont(fontData(f));
  loadedFont = (int)f;
}

int DisplayManager::text(const String& s, int x, int y, Font f, uint16_t color, uint8_t datum) {
  useFont(f);
  gfx->setTextColor(color);
  gfx->setTextDatum(datum);
  gfx->drawString(s, x, y);
  gfx->setTextDatum(TL_DATUM);
  return gfx->textWidth(s);
}

int DisplayManager::textWidth(const String& s, Font f) {
  useFont(f);
  return gfx->textWidth(s);
}

String DisplayManager::fit(const String& s, Font f, int maxW) {
  if (textWidth(s, f) <= maxW) return s;
  String t = s;
  while (t.length() > 0) {
    // Drop one character (all the bytes of a UTF-8 sequence) and any space before it
    int n = t.length() - 1;
    while (n > 0 && ((uint8_t)t[n] & 0xC0) == 0x80) n--;
    t.remove(n);
    while (t.length() && t[t.length() - 1] == ' ') t.remove(t.length() - 1);
    if (textWidth(t + "…", f) <= maxW) return t + "…";
  }
  return t;
}

void DisplayManager::label(const char* s, int x, int y, uint8_t datum) { text(s, x, y, Font::Caption, C_TEXT2, datum); }

void DisplayManager::centered(const String& s, int y, Font f, uint16_t color) {
  if (f == Font::Title && textWidth(s, f) > W - 16) f = Font::Body;  // long names: smaller font
  text(fit(s, f, W - 16), W / 2, y, f, color, TC_DATUM);
}

// ---------------------------------------------------------------------------
// Header, buttons

// Phone-style signal bars, right edge at `right`. Green = fix, yellow =
// searching, red outline = no data. Returns the x of their left edge.
static int signalBars(TFT_eSPI* g, const GPSHandler& gps, int right) {
  const int BARS = 5, BAR_W = 3, GAP = 2, MAX_H = 13;
  const int x0 = right - BARS * BAR_W - (BARS - 1) * GAP;
  const int baseY = HEADER_H / 2 + 7;

  int sats = gps.satellites();
  int lit = !gps.receivingData() ? 0 : sats >= 10 ? 5 : sats >= 8 ? 4 : sats >= 6 ? 3 : sats >= 4 ? 2 : sats >= 1 ? 1 : 0;
  uint16_t on = gps.hasFix() ? C_GREEN : C_YELLOW;
  uint16_t off = gps.receivingData() ? C_LINE : C_RED;

  for (int i = 0; i < BARS; i++) {
    int h = 5 + (MAX_H - 5) * i / (BARS - 1);
    int x = x0 + i * (BAR_W + GAP);
    if (i < lit) g->fillRect(x, baseY - h, BAR_W, h, on);
    else if (gps.receivingData()) g->fillRect(x, baseY - h, BAR_W, h, off);
    else g->drawRect(x, baseY - h, BAR_W, h, off);
  }
  return x0;
}

// A battery outline filled to `pct`, right edge at `right`. Returns its left edge.
static int batteryIcon(TFT_eSPI* g, int pct, uint16_t color, int right) {
  const int bw = 22, bh = 11;
  const int x = right - bw - 2, y = HEADER_H / 2 - bh / 2;
  g->drawRoundRect(x, y, bw, bh, 3, C_TEXT2);
  g->fillRect(x + bw, y + 3, 2, bh - 6, C_TEXT2);  // the nub
  int fill = (bw - 4) * constrain(pct, 0, 100) / 100;
  if (fill > 0) g->fillRoundRect(x + 2, y + 2, fill, bh - 4, 1, color);
  return x;
}

// GPS accuracy colour: green to 2 m, yellow to 5 m, red beyond
static uint16_t accuracyColor(float m) { return m <= 2.0f ? C_GREEN : m <= 5.0f ? C_YELLOW : C_RED; }

static String accuracyText(float m) {
  char buf[16];
  if (m < 10) snprintf(buf, sizeof(buf), "±%.1f m", m);
  else snprintf(buf, sizeof(buf), "±%.0f m", m < 99 ? m : 99);
  return String(buf);
}

void DisplayManager::header(const String& title, const GPSHandler* gps) {
  gfx->drawFastHLine(0, HEADER_H - 1, W, C_LINE);

  // Status, right to left: signal bars, GPS accuracy, battery, BENCH, BT
  int right = W - 8;
  auto tag = [&](const char* s, uint16_t bg, uint16_t fg) {
    int w = textWidth(s, Font::Caption) + 12;
    right -= w;
    gfx->fillRoundRect(right, HEADER_H / 2 - 8, w, 16, 8, bg);
    text(s, right + w / 2, HEADER_H / 2, Font::Caption, fg, MC_DATUM);
    right -= 6;
  };
  if (gps) {
    right = signalBars(gfx, *gps, right) - 6;
    if (gps->hasFix() && gps->accuracyM() >= 0) {  // u-blox: GPS accuracy beside the bars
      String acc = accuracyText(gps->accuracyM());
      right -= text(acc, right, HEADER_H / 2, Font::Caption, accuracyColor(gps->accuracyM()), MR_DATUM) + 8;
    }
  }
  if (batteryPct >= 0) {
    uint16_t c = batteryPct <= 15 ? C_RED : batteryPct <= 30 ? C_YELLOW : C_TEXT;
    right = batteryIcon(gfx, batteryPct, c, right) - 4;
    char b[8];
    snprintf(b, sizeof(b), "%d%%", batteryPct);
    right -= text(b, right, HEADER_H / 2, Font::Caption, batteryPct <= 30 ? c : C_TEXT2, MR_DATUM) + 8;
  }
  if (gps) {
    if (gps->isSimulated()) tag("BENCH", C_YELLOW, C_BG);  // simulated timing must be obvious
#if BLE_ENABLED
    if (RaceBoxBle::connected()) tag("BT", C_BLUE, C_TEXT);  // a phone app is receiving GPS
#endif
  }

  text(fit(title, Font::Body, right - 12), 10, HEADER_H / 2, Font::Body, C_TEXT, ML_DATUM);
}

// primary: white with black text, for the action the screen is about.
void DisplayManager::button(int x, int y, int w, int h, const char* label, bool primary, bool highlight) {
  gfx->fillRoundRect(x, y, w, h, CORNER, primary ? C_TEXT : highlight ? C_RAISED : C_SURFACE);
  if (highlight) gfx->drawRoundRect(x, y, w, h, CORNER, C_TEXT);
  text(fit(label, Font::Button, w - 8), x + w / 2, y + h / 2 + 1, Font::Button, primary ? C_BG : C_TEXT, MC_DATUM);
}

void DisplayManager::footerButtons(const char* left, const char* center, const char* right) {
  const int bw = W / 3;
  const char* labels[3] = {left, center, right};
  for (int i = 0; i < 3; i++) {
    if (!labels[i] || !labels[i][0]) continue;
    button(i * bw + 5, FOOTER_Y, bw - 10, FOOTER_H, labels[i], i == 1);
  }
}

// "Keep holding to end": a card with a bar that fills over END_SESSION_HOLD_MS.
void DisplayManager::holdBar(uint32_t holdMs, const char* what) {
  if (holdMs <= 150) return;
  const int x = 24, y = 80, w = W - 48, h = 64;
  int fill = (int)min((uint32_t)(w - 32), (uint32_t)((w - 32) * holdMs / END_SESSION_HOLD_MS));
  gfx->fillRoundRect(x - 1, y - 1, w + 2, h + 2, CORNER + 1, C_LINE);
  gfx->fillRoundRect(x, y, w, h, CORNER, C_RAISED);
  text(what, W / 2, y + 12, Font::Body, C_TEXT, TC_DATUM);
  gfx->fillRoundRect(x + 16, y + 40, w - 32, 8, 4, C_SURFACE);
  if (fill > 8) gfx->fillRoundRect(x + 16, y + 40, fill, 8, 4, C_ORANGE);
}

String DisplayManager::distanceText(double m) {
  if (m < 0) return String("");
  char buf[24];
  if (m < 1000) snprintf(buf, sizeof(buf), "%.0f m", m);
  else if (m < 100000) snprintf(buf, sizeof(buf), "%.1f km", m / 1000.0);
  else snprintf(buf, sizeof(buf), "%.0f km", m / 1000.0);
  return String(buf);
}

// "-1.23" with a true minus sign, or "+1.23"
static String deltaText(int32_t d) {
  uint32_t a = d < 0 ? -d : d;
  char buf[16];
  snprintf(buf, sizeof(buf), "%s%lu.%02lu", d < 0 ? "−" : d > 0 ? "+" : "", (unsigned long)(a / 1000),
           (unsigned long)((a % 1000) / 10));
  return String(buf);
}

static bool deltaShown(int32_t d, bool lapRunning) { return lapRunning && d != NO_DELTA && d > -100000 && d < 100000; }

// ---------------------------------------------------------------------------

void DisplayManager::drawBoot(const char* status) {
  if (splashRunning()) return;  // the logo animation owns the screen
  beginFrame();
  text("TRACK RAT", W / 2, 74, Font::Hero, C_TEXT, TC_DATUM);
  centered("GPS lap timer", 122, Font::Body, C_TEXT2);
  centered(status, 176, Font::Caption, C_TEXT3);
  endFrame();
}

void DisplayManager::drawGpsWait(const GPSHandler& gps, int trackCount, const char* trackSource,
                                 bool touchOk, bool statusOnly) {
  beginFrame();
  header(statusOnly ? "GPS status" : "Waiting for GPS", &gps);

  // A card of label / value rows
  const int cx = 8, cy = HEADER_H + 8, cw = W - 16, rowH = 27, rows = 5;
  const int vx = 98, vw = cx + cw - 12 - vx;
  gfx->fillRoundRect(cx, cy, cw, rowH * rows + 4, CORNER, C_SURFACE);
  int y = cy + 2 + rowH / 2;
  auto row = [&](const char* name, const String& value, uint16_t color) {
    label(name, cx + 12, y, ML_DATUM);
    text(fit(value, Font::Body, vw), vx, y, Font::Body, color, ML_DATUM);
    if (y + rowH < cy + rowH * rows) gfx->drawFastHLine(cx + 12, y + rowH / 2, cw - 24, C_LINE);
    y += rowH;
  };

  char buf[64];
  if (gps.receivingData()) {
    if (gps.isSimulated()) snprintf(buf, sizeof(buf), "Simulated, no GPS detected");
    else snprintf(buf, sizeof(buf), "%s, %lu baud, IO%d", gps.usingUbx() ? "UBX" : "NMEA",
                  (unsigned long)gps.baud(), gps.rxPinUsed());
    row("DATA", buf, C_GREEN);
  } else {
    row("DATA", "No data: check the wiring", C_RED);
  }

  // Fix, with position and speed accuracy from a u-blox
  const GpsFix& lf = gps.lastFix();
  label("FIX", cx + 12, y, ML_DATUM);
  int x = vx + text(gps.hasFix() ? "Locked" : "Searching…", vx, y, Font::Body, gps.hasFix() ? C_GREEN : C_YELLOW, ML_DATUM);
  if (gps.hasFix() && lf.accuracyKnown()) {
    x += 12 + text(accuracyText(lf.hAccM), x + 12, y, Font::Body, accuracyColor(lf.hAccM), ML_DATUM);
    snprintf(buf, sizeof(buf), lf.fixType == 2 ? "±%.1f %s  2D" : "±%.1f %s", Settings::speed(lf.speedAccMs * 3.6f), Settings::speedUnit());
    text(fit(buf, Font::Body, vx + vw - x - 12), x + 12, y, Font::Body, C_TEXT, ML_DATUM);
  }
  gfx->drawFastHLine(cx + 12, y + rowH / 2, cw - 24, C_LINE);
  y += rowH;

  snprintf(buf, sizeof(buf), "%d    %s %.1f    %.0f Hz", gps.satellites(), gps.usingUbx() ? "PDOP" : "HDOP",
           gps.hdop(), gps.fixRateHz());
  row("SATELLITES", buf, C_TEXT);
  if (lf.rxMillis != 0) snprintf(buf, sizeof(buf), "%.6f, %.6f", lf.lat, lf.lon);
  else snprintf(buf, sizeof(buf), "–");
  row("POSITION", buf, lf.rxMillis != 0 ? C_TEXT : C_TEXT3);
  snprintf(buf, sizeof(buf), "%d from %s, laps to %s", trackCount, trackSource, Storage::sessionFSName());
  row("TRACKS", buf, C_TEXT);

  // Satellites in view, one segment each
  const int bars = min((int)gps.satellites(), 12), segW = (W - 16 - 11 * 3) / 12;
  for (int i = 0; i < 12; i++) {
    uint16_t c = i < bars ? (gps.hasFix() ? C_GREEN : C_YELLOW) : C_SURFACE;
    gfx->fillRoundRect(8 + i * (segW + 3), cy + rowH * rows + 14, segW, 6, 3, c);
  }

  if (statusOnly) centered(touchOk ? "Tap to go back" : "Press BOOT to go back", 214, Font::Body, C_TEXT3);
  else centered(touchOk ? "Tap or press BOOT for the menu" : "Press BOOT for the menu", 214, Font::Body, C_TEXT3);
  endFrame();
}

void DisplayManager::drawTrackSelect(const Track& track, int index, int count, double distanceM,
                                     const GPSHandler& gps, bool touchOk) {
  beginFrame();
  char buf[40];
  snprintf(buf, sizeof(buf), "Tracks   %d of %d", index + 1, count);
  header(buf, &gps);  // tap the top bar for GPS status

  centered(track.name, HEADER_H + 10, Font::Title, C_TEXT);
  String where = track.location + (track.country.length() ? ", " + track.country : String(""));
  if (track.layoutName.length()) {
    centered(track.layoutName + " layout", HEADER_H + 40, Font::Body, C_GREEN);
    centered(where, HEADER_H + 60, Font::Caption, C_TEXT2);
  } else {
    centered(where, HEADER_H + 42, Font::Body, C_TEXT2);
  }

  // Length, distance and splits as a row of three figures
  const int sy = 110, sh = 50, colW = (W - 16) / 3;
  gfx->fillRoundRect(8, sy, W - 16, sh, CORNER, C_SURFACE);
  for (int i = 1; i < 3; i++) gfx->drawFastVLine(8 + i * colW, sy + 10, sh - 20, C_LINE);
  auto stat = [&](int col, const char* name, const String& value, uint16_t color) {
    int x = 8 + col * colW + colW / 2;
    label(name, x, sy + 8, TC_DATUM);
    text(value, x, sy + 24, Font::Button, color, TC_DATUM);
  };
  if (track.length > 0) snprintf(buf, sizeof(buf), "%.2f km", track.length / 1000.0);
  stat(0, "LENGTH", track.length > 0 ? String(buf) : String("–"), track.length > 0 ? C_TEXT : C_TEXT3);
  stat(1, "FROM YOU", distanceM >= 0 ? distanceText(distanceM) : String("–"), distanceM >= 0 ? C_TEXT : C_TEXT3);
  snprintf(buf, sizeof(buf), "%d", (int)track.splits.size());
  stat(2, "SPLITS", buf, C_TEXT);

  if (!track.hasStartFinish && !track.bench) centered("No start/finish line yet: set it on track", 170, Font::Caption, C_YELLOW);
  else if (touchOk) centered("Tap the top bar for GPS status", 170, Font::Caption, C_TEXT3);

  if (touchOk) footerButtons("‹", "Select", "›");
  else centered("BOOT: press for next, hold to select", 212, Font::Body, C_TEXT3);
  endFrame();
}

void DisplayManager::drawTrackConfirm(const Track& track, double distanceM, bool gpsFix, bool touchOk,
                                      int layoutNumber, int layoutCount) {
  beginFrame();
  char buf[64];
  if (layoutCount > 1 && touchOk) {
    snprintf(buf, sizeof(buf), "Ready   ·   layout %d/%d (tap for next)", layoutNumber + 1, layoutCount);
    header(buf, nullptr);
  } else {
    header("Ready", nullptr);
  }

  centered(track.name, HEADER_H + 10, Font::Title, C_TEXT);
  int y = HEADER_H + 40;
  if (track.layoutName.length()) {
    centered(track.layoutName + " layout", y, Font::Body, C_GREEN);
    y += 22;
  }

  // Status pill: how far away the line is
  const char* what = track.hasStartFinish ? "Start/finish" : "Track";
  String status;
  uint16_t color;
  if (track.bench) status = "Simulated car, no GPS needed", color = C_YELLOW;
  else if (gpsFix && distanceM >= 0) status = String(what) + " is " + distanceText(distanceM) + " away", color = distanceM < 2000 ? C_GREEN : C_YELLOW;
  else status = "No GPS fix yet", color = C_YELLOW;
  int pw = textWidth(status, Font::Body) + 34;
  gfx->fillRoundRect((W - pw) / 2, y, pw, 26, 13, C_SURFACE);
  gfx->fillCircle((W - pw) / 2 + 14, y + 13, 4, color);
  text(status, (W - pw) / 2 + 26, y + 13, Font::Body, C_TEXT, ML_DATUM);

  y = max(y + 40, 118);
  if (track.hasStartFinish) {
    centered("Timing starts when you cross", y, Font::Body, C_TEXT);
    centered("the start/finish line", y + 19, Font::Body, C_TEXT);
  } else {
    centered("No start/finish line yet:", y, Font::Body, C_YELLOW);
    centered("tap the screen as you cross it", y + 19, Font::Body, C_TEXT);
  }
  centered(String("Laps are saved to ") + Storage::sessionFSName(), y + 46, Font::Caption, C_TEXT3);

  if (touchOk) footerButtons("Back", "Start", track.bench ? "" : "Set line");
  else centered("BOOT: hold to start, press to go back", 212, Font::Body, C_TEXT3);
  endFrame();
}

void DisplayManager::drawRunning(const Track& track, const LapTimer& timer, const GPSHandler& gps,
                                 const RunningOverlay& ov, uint32_t holdMs) {
  beginFrame();
  char buf[40];
  header(runningTitle(track, timer), &gps);

  // The Big number setting: what gets the largest digits. Delta and predicted
  // lap need a live delta against a best lap; until then it's the lap time.
  const int32_t liveDelta = gps.hasFix() ? timer.liveDeltaMs() : NO_DELTA;  // without a fix it's stale
  const bool haveDelta = deltaShown(liveDelta, timer.lapRunning()) && timer.bestLap();
  const BigNumber big = settings::bigNumberShown(Settings::get().bigNumber, haveDelta);

  // --- big time area (y 28..106) ---
  const int top = HEADER_H + 4;
  if (ov.showLap) {
    // The lap just finished, on a full-width band: green for a new best
    uint16_t bg = ov.lapIsBest ? C_GREEN : C_RAISED, fg = ov.lapIsBest ? C_BG : C_TEXT;
    gfx->fillRoundRect(4, top, W - 8, 74, CORNER, bg);
    snprintf(buf, sizeof(buf), ov.lapIsBest ? "LAP %d  ·  BEST LAP" : "LAP %d", ov.lapNumber);
    text(buf, W / 2, top + 8, Font::Caption, fg, TC_DATUM);
    text(formatLapTime(ov.lapMs, 3), W / 2, top + 26, Font::NumL, fg, TC_DATUM);
  } else if (big == BigNumber::Delta) {
    // The live diff on a full-width band, as the diff box below does it: green ahead, red behind
    gfx->fillRoundRect(4, top, W - 8, 74, CORNER, liveDelta <= 0 ? C_GREEN : C_RED);
    String t = deltaText(liveDelta);
    Font f = textWidth(t, Font::NumXL) <= W - 24 ? Font::NumXL : Font::NumL;
    text(t, W / 2, f == Font::NumXL ? top + 8 : top + 20, f, C_BG, TC_DATUM);
  } else if (timer.lapRunning()) {
    const bool predicted = big == BigNumber::Predicted;
    String t = formatLapTime(predicted ? settings::predictedLapMs(timer.bestLap()->timeMs, liveDelta)
                                       : timer.currentLapMs(millis()), 1);
    Font f = textWidth(t, Font::NumXL) <= W - 10 ? Font::NumXL : Font::NumL;
    text(t, W / 2, f == Font::NumXL ? top + (predicted ? 17 : 8) : top + 18, f, C_TEXT, TC_DATUM);  // predicted: room for its label
    if (predicted) label("PREDICTED", 12, top);
  } else {
    centered("Out lap", top + 16, Font::Title, C_YELLOW);
    centered("Cross the start/finish line to begin timing", top + 48, Font::Body, C_TEXT2);
  }

  // --- last / best (y 110..150) ---
  const int rowY = 112;
  gfx->drawFastVLine(W / 2, rowY + 2, 34, C_LINE);
  label("LAST", 12, rowY);
  label("BEST", W / 2 + 12, rowY);
  const Lap* last = timer.lastLap();
  const Lap* best = timer.bestLap();
  text(last ? formatLapTime(last->timeMs) : String("–"), 12, rowY + 15, Font::Title, last ? C_TEXT : C_TEXT3);
  text(best ? formatLapTime(best->timeMs) : String("–"), W / 2 + 12, rowY + 15, Font::Title,
       best ? C_GREEN : C_TEXT3);

  // --- live diff box (y 156..236); with the delta as the big number, the lap time goes here ---
  if (big == BigNumber::Delta && !ov.showLap) {
    const int x = 4, y = 156, w = W - 8, h = H - 156 - 4;
    gfx->fillRoundRect(x, y, w, h, CORNER + 2, C_SURFACE);
    label("LAP TIME", x + 12, y + 9);
    text(formatLapTime(timer.currentLapMs(millis()), 1), W / 2, y + 23, Font::NumL, C_TEXT, TC_DATUM);
  } else {
    diffBox(liveDelta, timer.lapRunning(), ov);
  }

  runningOverlays(gps, holdMs, &ov);
  endFrame();
}

void DisplayManager::runningOverlays(const GPSHandler& gps, uint32_t holdMs, const RunningOverlay* ov) {
  if (!gps.hasFix()) {
    gfx->fillRoundRect(4, H - 34, W - 8, 30, CORNER, C_RED);
    text("No GPS fix", W / 2, H - 18, Font::Button, C_BG, MC_DATUM);
  }

  if (ov && ov->notice.length() && holdMs <= 150) {
    gfx->fillRoundRect(10, 156, W - 20, 60, CORNER, C_YELLOW);  // over the delta box, below the lap times
    text(fit(ov->notice, Font::Title, W - 40), W / 2, 164, Font::Title, C_BG, TC_DATUM);
    text("Matched your lap, switched", W / 2, 192, Font::Body, C_BG, TC_DATUM);
  }

  holdBar(holdMs, "Keep holding to end the session");
}

String DisplayManager::runningTitle(const Track& track, const LapTimer& timer) {
  if (!timer.lapRunning()) return track.displayName();
  char buf[16];
  snprintf(buf, sizeof(buf), "Lap %d   ", timer.currentLapNumber());
  return buf + track.displayName();  // lap number first so a long name gets shortened, not the lap
}

// ---------------------------------------------------------------------------
// Map view: track layout on the left, timing on the right.

static const int MAP_X = 4, MAP_Y = HEADER_H + 4, MAP_W = 210, MAP_H = H - HEADER_H - 8;
static const int PANEL_X = MAP_X + MAP_W + 8;
static const int PANEL_W = W - PANEL_X - 4;
static const layout::Lines NO_LINES;  // Free Roam has a path but no stored layout

// Draws stored lines and/or a path (points whose first two members are float
// x, y in metres) scaled to fit the box, north up, plus the start/finish
// marker at the origin and the car. Returns false if there was nothing to draw.
bool DisplayManager::drawMapArea(int bx, int by, int bw, int bh, const std::vector<std::vector<geo::XYf>>& lines,
                                 MapPath path, bool showCar, geo::XY car, bool carFix, bool showStartFinish) {
  gfx->fillRoundRect(bx, by, bw, bh, CORNER, C_SURFACE);
  auto pt = [&](size_t i) { return (const float*)(path.base + i * path.stride); };

  float minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
  auto grow = [&](float x, float y) {
    minX = min(minX, x); maxX = max(maxX, x);
    minY = min(minY, y); maxY = max(maxY, y);
  };
  for (const auto& line : lines)
    for (const auto& p : line) grow(p.x, p.y);
  for (size_t i = 0; i < path.n; i++) grow(pt(i)[0], pt(i)[1]);
  if (!(maxX > minX || maxY > minY)) return false;

  const float pad = 14;
  float sw = max(maxX - minX, 1.0f), sh = max(maxY - minY, 1.0f);
  float scale = min((bw - 2 * pad) / sw, (bh - 2 * pad) / sh);
  float cx = bx + bw / 2.0f, cy = by + bh / 2.0f;
  float midX = (minX + maxX) / 2, midY = (minY + maxY) / 2;
  auto sx = [&](float x) { return (int)lroundf(cx + (x - midX) * scale); };
  auto sy = [&](float y) { return (int)lroundf(cy - (y - midY) * scale); };  // north up
  auto thickLine = [&](int x0, int y0, int x1, int y1) {  // 3 px wide
    gfx->drawLine(x0, y0, x1, y1, C_TRACK);
    gfx->drawLine(x0 + 1, y0, x1 + 1, y1, C_TRACK);
    gfx->drawLine(x0, y0 + 1, x1, y1 + 1, C_TRACK);
    gfx->drawLine(x0 + 1, y0 + 1, x1 + 1, y1 + 1, C_TRACK);
  };

  for (const auto& line : lines)
    for (size_t i = 1; i < line.size(); i++) thickLine(sx(line[i - 1].x), sy(line[i - 1].y), sx(line[i].x), sy(line[i].y));

  if (path.n >= 2) {
    size_t step = max((size_t)1, path.n / 800);
    size_t prev = 0;
    for (size_t i = step; i < path.n; i += step) {
      thickLine(sx(pt(prev)[0]), sy(pt(prev)[1]), sx(pt(i)[0]), sy(pt(i)[1]));
      prev = i;
    }
    if (prev != path.n - 1) thickLine(sx(pt(prev)[0]), sy(pt(prev)[1]), sx(pt(path.n - 1)[0]), sy(pt(path.n - 1)[1]));
  }

  if (showStartFinish) {  // a small chequered flag
    int x = sx(0) - 5, y = sy(0) - 5;
    gfx->fillRect(x - 1, y - 1, 12, 12, C_BG);
    for (int i = 0; i < 4; i++)
      gfx->fillRect(x + (i % 2) * 5, y + (i / 2) * 5, 5, 5, ((i % 2) ^ (i / 2)) ? C_BG : C_TEXT);
    gfx->drawRect(x, y, 10, 10, C_TEXT);
  }

  if (showCar) {  // kept inside the box if it's off the drawn area
    int px = constrain(sx(car.x), bx + 8, bx + bw - 9);
    int py = constrain(sy(car.y), by + 8, by + bh - 9);
    gfx->fillCircle(px, py, 8, C_BG);  // dark ring keeps it visible on the track line
    gfx->fillCircle(px, py, 6, carFix ? C_TEXT : C_YELLOW);
  }
  return true;
}

void DisplayManager::drawRunningMap(const Track& track, const LapTimer& timer, const GPSHandler& gps,
                                    const RunningOverlay& ov, uint32_t holdMs) {
  beginFrame();
  header(runningTitle(track, timer), &gps);

  // What to draw: the stored layout, else the best lap's path, else the
  // path driven so far this lap.
  const std::vector<LapTimer::TracePt>* path = nullptr;
  if (track.map.empty()) {
    if (timer.bestPath().size() >= 2) path = &timer.bestPath();
    else if (timer.currentPath().size() >= 2) path = &timer.currentPath();
  }
  MapPath mp = {nullptr, 0, 0};
  if (path) mp = {(const uint8_t*)path->data(), path->size(), sizeof(LapTimer::TracePt)};
  geo::XY car = geo::toLocal(gps.lastFix().lat, gps.lastFix().lon, track.refLat(), track.refLon());
  bool drawn = drawMapArea(MAP_X, MAP_Y, MAP_W, MAP_H, track.map, mp, gps.lastFix().rxMillis != 0, car,
                           gps.hasFix(), track.hasStartFinish);
  if (!drawn) {
    const int mx = MAP_X + MAP_W / 2, my = MAP_Y + MAP_H / 2;
    text("No map for this track", mx, my - 20, Font::Body, C_TEXT2, MC_DATUM);
    text("It draws itself once", mx, my + 4, Font::Caption, C_TEXT3, MC_DATUM);
    text("your first lap starts", mx, my + 20, Font::Caption, C_TEXT3, MC_DATUM);
  }

  // --- right-hand panel ---
  int y = MAP_Y;
  if (ov.showLap) {
    uint16_t bg = ov.lapIsBest ? C_GREEN : C_RAISED, fg = ov.lapIsBest ? C_BG : C_TEXT;
    gfx->fillRoundRect(PANEL_X - 4, y, PANEL_W + 4, 46, CORNER, bg);
    char buf[16];
    snprintf(buf, sizeof(buf), ov.lapIsBest ? "LAP %d BEST" : "LAP %d", ov.lapNumber);
    text(buf, PANEL_X + 2, y + 6, Font::Caption, fg);
    text(formatLapTime(ov.lapMs, 1), PANEL_X + 2, y + 20, Font::Title, fg);
  } else {
    label(timer.lapRunning() ? "LAP TIME" : "OUT LAP", PANEL_X + 2, y + 6);
    text(timer.lapRunning() ? formatLapTime(timer.currentLapMs(millis()), 1) : String("–"), PANEL_X + 2, y + 20,
         Font::Title, C_TEXT);
  }

  y += 52;
  int32_t d = gps.hasFix() ? timer.liveDeltaMs() : NO_DELTA;
  bool haveDelta = deltaShown(d, timer.lapRunning());
  uint16_t bg = !haveDelta ? C_SURFACE : (d <= 0 ? C_GREEN : C_RED), fg = haveDelta ? C_BG : C_TEXT3;
  gfx->fillRoundRect(PANEL_X - 4, y, PANEL_W + 4, 48, CORNER, bg);
  text("LIVE DIFF", PANEL_X + 2, y + 6, Font::Caption, haveDelta ? C_BG : C_TEXT2);
  text(haveDelta ? deltaText(d) : String("–"), PANEL_X + 2, y + 21, Font::Title, fg);

  // For a few seconds after a split: its gap to the best lap's split
  y += 56;
  if (ov.splitIndex >= 0) {
    char buf[24];
    snprintf(buf, sizeof(buf), "S%d %s", ov.splitIndex + 1, ov.deltaMs == NO_DELTA ? "" : formatDelta(ov.deltaMs).c_str());
    gfx->fillRoundRect(PANEL_X - 4, y, PANEL_W + 4, 26, CORNER, C_SURFACE);
    text(buf, PANEL_X + 2, y + 13, Font::Body, ov.deltaMs == NO_DELTA ? C_TEXT : (ov.deltaMs <= 0 ? C_GREEN : C_RED),
         ML_DATUM);
  } else {
    const Lap* best = timer.bestLap();
    const Lap* last = timer.lastLap();
    label("BEST", PANEL_X + 2, y);
    text(best ? formatLapTime(best->timeMs) : String("–"), PANEL_X + 2, y + 14, Font::Body, best ? C_GREEN : C_TEXT3);
    label("LAST", PANEL_X + 2, y + 38);
    text(last ? formatLapTime(last->timeMs) : String("–"), PANEL_X + 2, y + 52, Font::Body, last ? C_TEXT : C_TEXT3);
  }

  text("Tap: timing", PANEL_X + 2, H - 6, Font::Caption, C_TEXT3, BL_DATUM);

  runningOverlays(gps, holdMs, &ov);
  endFrame();
}

// Live gap to the best lap: green = ahead, red = behind, black digits.
void DisplayManager::diffBox(int32_t delta, bool lapRunning, const RunningOverlay& ov) {
  const int x = 4, y = 156, w = W - 8, h = H - 156 - 4;
  bool have = deltaShown(delta, lapRunning);
  uint16_t bg = !have ? C_SURFACE : (delta <= 0 ? C_GREEN : C_RED);
  uint16_t fg = have ? C_BG : C_TEXT2;
  gfx->fillRoundRect(x, y, w, h, CORNER + 2, bg);
  text("LIVE DIFF", x + 12, y + 9, Font::Caption, fg);

  // Split result for a few seconds after each split
  if (ov.splitIndex >= 0) {
    char sbuf[40];
    snprintf(sbuf, sizeof(sbuf), "S%d  %s  %s", ov.splitIndex + 1, formatLapTime(ov.splitMs).c_str(),
             ov.deltaMs == NO_DELTA ? "" : formatDelta(ov.deltaMs).c_str());
    text(sbuf, x + w - 12, y + 9, Font::Caption, have ? C_BG : C_TEXT, TR_DATUM);
  }

  if (!have) {
    if (lapRunning) text("–", W / 2, y + h / 2 - 2, Font::Title, C_TEXT3, MC_DATUM);
    text("Tap: map   ·   Hold: end session", W / 2, y + h - 8, Font::Caption, C_TEXT3, BC_DATUM);
    return;
  }
  text(deltaText(delta), W / 2, y + 23, Font::NumL, C_BG, TC_DATUM);
}

// ---------------------------------------------------------------------------
// Mode menu, Free Roam, setting a start/finish line

static String formatDuration(uint32_t ms) {
  uint32_t s = ms / 1000;
  char buf[16];
  if (s >= 3600) snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  else snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  return String(buf);
}

// Small line icons for the menu tiles, centred on cx, cy.
static void menuIcon(TFT_eSPI* g, int which, int cx, int cy, uint16_t c) {
  if (which == 0) {  // Free Roam: a winding route ending in a dot
    const int pts[][2] = {{-14, 8}, {-8, 0}, {-2, 4}, {4, -4}, {10, -2}, {14, -9}};
    for (int i = 1; i < 6; i++)
      for (int t = 0; t < 2; t++) g->drawLine(cx + pts[i - 1][0], cy + pts[i - 1][1] + t, cx + pts[i][0], cy + pts[i][1] + t, c);
    g->fillCircle(cx + 14, cy - 9, 3, c);
    g->fillCircle(cx - 14, cy + 8, 2, c);
  } else if (which == 1) {  // New Track: a chequered flag
    g->fillRect(cx - 11, cy - 11, 2, 23, c);
    for (int r = 0; r < 3; r++)
      for (int k = 0; k < 4; k++)
        if ((r + k) % 2 == 0) g->fillRect(cx - 9 + k * 5, cy - 11 + r * 5, 5, 5, c);
    g->drawRect(cx - 9, cy - 11, 20, 15, c);
  } else {  // Track list: three rows
    for (int r = 0; r < 3; r++) {
      g->fillCircle(cx - 11, cy - 8 + r * 8, 2, c);
      g->fillRoundRect(cx - 6, cy - 9 + r * 8, 19, 3, 1, c);
    }
  }
}

void DisplayManager::drawMenu(int highlight, const GPSHandler& gps, bool noTrackNearby, bool touchOk) {
  beginFrame();
  header(noTrackNearby ? "No track within 5 km" : "Menu", &gps);

  struct Item { const char* title; const char* d1; const char* d2; };
  const Item items[3] = {
    {"Free Roam", "Map your drive,", "time and speed"},
    {"New Track", "Tap the screen at", "the start/finish"},
    {"Track List", "Pick a track or", "Bench Test"},
  };
  const int bw = W / 3, top = HEADER_H + 6, bh = MENU_WIFI_Y - top - 6;  // the second row starts at MENU_WIFI_Y
  for (int i = 0; i < 3; i++) {
    int x = i * bw + 4, w = bw - 8, mid = x + w / 2;
    bool hl = (i == highlight);
    gfx->fillRoundRect(x, top, w, bh, CORNER + 2, hl ? C_RAISED : C_SURFACE);
    if (hl) gfx->drawRoundRect(x, top, w, bh, CORNER + 2, C_TEXT);
    bool needsFix = i < 2 && !gps.hasFix();
    menuIcon(gfx, i, mid, top + 30, needsFix ? C_TEXT3 : C_TEXT);
    text(items[i].title, mid, top + 58, Font::Button, needsFix ? C_TEXT2 : C_TEXT, TC_DATUM);
    text(items[i].d1, mid, top + 82, Font::Caption, C_TEXT2, TC_DATUM);
    text(items[i].d2, mid, top + 96, Font::Caption, C_TEXT2, TC_DATUM);
  }
  // The second row, in thirds like the taps: Performance, Settings, Wi-Fi
  auto rowButton = [&](int x, const char* title, bool hl) {
    if (textWidth(title, Font::Button) <= bw - 16) return button(x, MENU_WIFI_Y, bw - 8, MENU_ROW_H, title, false, hl);
    gfx->fillRoundRect(x, MENU_WIFI_Y, bw - 8, MENU_ROW_H, CORNER, hl ? C_RAISED : C_SURFACE);  // too wide for the button font
    if (hl) gfx->drawRoundRect(x, MENU_WIFI_Y, bw - 8, MENU_ROW_H, CORNER, C_TEXT);
    text(title, x + (bw - 8) / 2, MENU_WIFI_Y + MENU_ROW_H / 2 + 1, Font::Body, C_TEXT, MC_DATUM);
  };
  rowButton(4, "Performance", highlight == 3);
  rowButton(bw + 4, "Settings", highlight == 4);
  wifiButton(MENU_WIFI_Y, MENU_ROW_H, highlight == 5);
  if (!gps.hasFix()) centered("Free Roam and New Track need a GPS fix", 196, Font::Caption, C_YELLOW);
  centered(touchOk ? "Tap a mode   ·   top bar: GPS status" : "BOOT: press for next, hold to choose", 216, Font::Body,
           C_TEXT3);
  endFrame();
}

void DisplayManager::drawFreeRoam(const FreeRoam& fr, const GPSHandler& gps, uint32_t holdMs) {
  beginFrame();
  header("Free Roam", &gps);

  MapPath mp = {(const uint8_t*)fr.path.data(), fr.path.size(), sizeof(geo::XYf)};
  geo::XY car = fr.hasPosition() ? geo::toLocal(gps.lastFix().lat, gps.lastFix().lon, fr.refLat, fr.refLon) : geo::XY{0, 0};
  if (!drawMapArea(MAP_X, MAP_Y, MAP_W, MAP_H, NO_LINES, mp, fr.hasPosition(), car, gps.hasFix(), false)) {
    const int mx = MAP_X + MAP_W / 2, my = MAP_Y + MAP_H / 2;
    text(fr.hasPosition() ? "Start driving" : "Waiting for GPS", mx, my - 10, Font::Body, C_TEXT2, MC_DATUM);
    if (fr.hasPosition()) text("Your route draws here", mx, my + 12, Font::Caption, C_TEXT3, MC_DATUM);
  }

  int y = MAP_Y + 6;
  label("TIME", PANEL_X + 2, y);
  text(formatDuration(fr.elapsedMs(millis())), PANEL_X + 2, y + 14, Font::Title, C_TEXT);

  y += 50;
  label(Settings::get().units == SpeedUnits::Mph ? "MPH" : "KM/H", PANEL_X + 2, y);
  text(String((long)lroundf(Settings::speed(gps.lastFix().speedKmh))), PANEL_X + 2, y + 16, Font::NumL, C_TEXT);

  y += 66;
  label("DISTANCE", PANEL_X + 2, y);
  char buf[24];
  snprintf(buf, sizeof(buf), "%.1f km", fr.distanceKm());
  text(buf, PANEL_X + 2, y + 14, Font::Title, C_TEXT);

  snprintf(buf, sizeof(buf), "Top %.0f %s", Settings::speed(fr.topSpeedKmh()), Settings::speedUnit());
  text(buf, PANEL_X + 2, H - 24, Font::Caption, C_TEXT2, BL_DATUM);
  text("Hold to end", PANEL_X + 2, H - 6, Font::Caption, C_TEXT3, BL_DATUM);

  runningOverlays(gps, holdMs);
  endFrame();
}

void DisplayManager::drawFreeRoamSummary(const FreeRoam& fr, uint32_t durationMs, const String& savedPath) {
  beginFrame();
  // In the title, so it doesn't cover the route on the map
  header(savedPath.length() ? "Drive saved   ·   tap to continue" : "Drive not saved   ·   tap to continue", nullptr);
  MapPath mp = {(const uint8_t*)fr.path.data(), fr.path.size(), sizeof(geo::XYf)};
  if (!drawMapArea(MAP_X, MAP_Y, MAP_W, MAP_H, NO_LINES, mp, false, geo::XY{0, 0}, true, false))
    text("No route recorded", MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2, Font::Body, C_TEXT3, MC_DATUM);

  char buf[24];
  struct Row { const char* label; String value; };
  snprintf(buf, sizeof(buf), "%.2f km", fr.distanceKm());
  String dist = buf;
  snprintf(buf, sizeof(buf), "%.0f %s", Settings::speed(fr.topSpeedKmh()), Settings::speedUnit());
  String top = buf;
  snprintf(buf, sizeof(buf), "%.0f %s", Settings::speed(fr.avgMovingKmh()), Settings::speedUnit());
  String avg = buf;
  const Row rows[4] = {{"TIME", formatDuration(durationMs)}, {"DISTANCE", dist}, {"TOP SPEED", top}, {"AVERAGE", avg}};
  int y = MAP_Y + 4;
  for (const Row& r : rows) {
    label(r.label, PANEL_X + 2, y);
    text(fit(r.value, Font::Title, PANEL_W), PANEL_X + 2, y + 13, Font::Title, C_TEXT);
    y += 40;
  }
  wifiButton(WIFI_BUTTON_Y, H - WIFI_BUTTON_Y - 4, false, true);
  endFrame();
}

void DisplayManager::drawSetLine(const Track& track, const GPSHandler& gps, uint32_t holdMs) {
  beginFrame();
  header(track.displayName(), &gps);

  gfx->fillRoundRect(8, HEADER_H + 8, W - 16, 112, CORNER + 2, C_SURFACE);
  centered("Set start/finish", HEADER_H + 20, Font::Title, C_YELLOW);
  centered("Drive to the start/finish line and", HEADER_H + 54, Font::Body, C_TEXT);
  centered("tap the screen as you cross it", HEADER_H + 73, Font::Body, C_TEXT);
  centered("Lap 1 starts at that moment", HEADER_H + 96, Font::Caption, C_TEXT2);

  char buf[24];
  snprintf(buf, sizeof(buf), "%.0f", Settings::speed(gps.lastFix().speedKmh));
  int w = textWidth(buf, Font::NumL);
  text(buf, W / 2 - 14, 154, Font::NumL, C_TEXT, TC_DATUM);
  label(Settings::get().units == SpeedUnits::Mph ? "MPH" : "KM/H", W / 2 - 14 + w / 2 + 8, 154 + 37, BL_DATUM);
  text(gps.hasFix() ? "GPS ready" : "Waiting for a GPS fix…", 12, H - 8, Font::Caption,
       gps.hasFix() ? C_GREEN : C_YELLOW, BL_DATUM);
  text("Hold to end", W - 12, H - 8, Font::Caption, C_TEXT3, BR_DATUM);

  runningOverlays(gps, holdMs);
  endFrame();
}

void DisplayManager::drawSummary(const Track& track, const LapTimer& timer, const String& savedPath) {
  beginFrame();
  header("Session summary   ·   " + track.displayName(), nullptr);

  char buf[48];
  const Lap* best = timer.bestLap();
  // Laps, best and average across the top
  int y = HEADER_H + 6;
  label("LAPS", 12, y);
  label("BEST", 76, y);
  label("AVERAGE", 196, y);
  text(String((int)timer.lapCount()), 12, y + 14, Font::Title, C_TEXT);
  text(best ? formatLapTime(best->timeMs) : String("–"), 76, y + 14, Font::Title, best ? C_GREEN : C_TEXT3);
  text(timer.lapCount() ? formatLapTime(timer.averageLapMs()) : String("–"), 196, y + 14, Font::Title,
       timer.lapCount() ? C_TEXT : C_TEXT3);

  // The latest laps, newest first
  const int ly = 76, rowH = 20, maxRows = 5;
  gfx->fillRoundRect(8, ly, W - 16, rowH * maxRows + 8, CORNER, C_SURFACE);
  y = ly + 4 + rowH / 2;
  int rows = 0;
  for (int i = (int)timer.lapCount() - 1; i >= 0 && rows < maxRows; i--, rows++) {
    const Lap& l = timer.lap(i);
    bool isBest = (i == timer.bestLapIndex());
    snprintf(buf, sizeof(buf), "Lap %d", l.number);
    text(buf, 20, y, Font::Body, C_TEXT2, ML_DATUM);
    text(formatLapTime(l.timeMs), 92, y, Font::Body, isBest ? C_GREEN : C_TEXT, ML_DATUM);
    if (isBest) text("BEST", 168, y, Font::Caption, C_GREEN, ML_DATUM);
    snprintf(buf, sizeof(buf), "top %.0f %s", Settings::speed(l.maxSpeedKmh), Settings::speedUnit());
    text(buf, W - 20, y, Font::Body, C_TEXT3, MR_DATUM);
    if (rows < maxRows - 1 && i > 0) gfx->drawFastHLine(20, y + rowH / 2, W - 40, C_LINE);
    y += rowH;
  }
  if (timer.lapCount() == 0) text("No complete laps recorded", W / 2, ly + (rowH * maxRows + 8) / 2, Font::Body, C_TEXT3, MC_DATUM);

  String saved = savedPath.length() ? "Saved to " + String(Storage::sessionFSName()) + ": " + savedPath : String("Not saved");
  text(fit(saved, Font::Caption, W - 24), 12, 186, Font::Caption, C_TEXT3, ML_DATUM);
  text("Tap or press BOOT", W / 3, WIFI_BUTTON_Y + 12, Font::Body, C_TEXT, MC_DATUM);
  text("to continue", W / 3, WIFI_BUTTON_Y + 30, Font::Body, C_TEXT2, MC_DATUM);
  wifiButton(WIFI_BUTTON_Y, H - WIFI_BUTTON_Y - 4, false, true);
  endFrame();
}

// ---------------------------------------------------------------------------
// Wi-Fi page

// Three curved bars, the Wi-Fi symbol, centred on cx with its point at y.
static void wifiIcon(TFT_eSPI* g, int cx, int y, uint16_t c) {
  g->fillCircle(cx, y - 2, 2, c);
  for (int r = 6; r <= 14; r += 4)
    for (int a = -45; a <= 45; a += 3) {
      float rad = (a - 90) * 0.0174533f;
      g->fillRect(cx + (int)lroundf(cosf(rad) * r), y - 2 + (int)lroundf(sinf(rad) * r), 2, 2, c);
    }
}

// holdHint: on the summaries, holding BOOT does the same (for boards without touch)
void DisplayManager::wifiButton(int y, int h, bool highlight, bool holdHint) {
  const int bw = W / 3, x = 2 * bw + 4, w = bw - 8;
  gfx->fillRoundRect(x, y, w, h, CORNER, highlight ? C_RAISED : C_SURFACE);
  gfx->drawRoundRect(x, y, w, h, CORNER, highlight ? C_TEXT : C_BLUE);
  int lw = textWidth("Wi-Fi", Font::Button) + 22;
  int cy = holdHint ? y + h / 2 - 6 : y + h / 2;
  wifiIcon(gfx, x + (w - lw) / 2 + 7, cy + 6, C_BLUE);
  text("Wi-Fi", x + (w - lw) / 2 + 22, cy + 1, Font::Button, C_TEXT, ML_DATUM);
  if (holdHint) text("or hold BOOT", x + w / 2, y + h - 5, Font::Caption, C_TEXT3, BC_DATUM);
}

void DisplayManager::drawWifi(bool wifiOn, const String& network, const String& address, int phones,
                              int uploadCode, int progress, bool restarting, bool touchOk) {
  beginFrame();
  header(wifiOn ? "Wi-Fi on" : "Wi-Fi", nullptr);
  if (!wifiOn) {
    centered("Wi-Fi didn't start", 80, Font::Title, C_RED);
    centered("The serial log says why", 116, Font::Body, C_TEXT2);
    if (touchOk) footerButtons("", "Back", "");
    else centered("Press BOOT to go back", 212, Font::Body, C_TEXT3);
    endFrame();
    return;
  }

  // Where to go, as two numbered steps
  const int cx = 8, cw = W - 16;
  gfx->fillRoundRect(cx, HEADER_H + 6, cw, 98, CORNER, C_SURFACE);
  auto step = [&](int n, int y, const char* hint, const String& value) {
    gfx->fillCircle(cx + 20, y + 16, 10, C_BLUE);
    text(String(n), cx + 20, y + 17, Font::Caption, C_TEXT, MC_DATUM);
    label(hint, cx + 40, y);
    text(fit(value, Font::Title, cw - 50), cx + 40, y + 13, Font::Title, C_TEXT);
  };
  step(1, HEADER_H + 12, "ON YOUR PHONE, JOIN THE NETWORK", network);
  gfx->drawFastHLine(cx + 40, HEADER_H + 55, cw - 52, C_LINE);
  step(2, HEADER_H + 60, "THE PAGE OPENS BY ITSELF, OR GO TO", "http://" + address);

  char buf[40];
  const int y = HEADER_H + 112;
  if (restarting) {
    centered("Restarting…", y + 10, Font::Title, C_YELLOW);
  } else if (progress >= 0) {
    snprintf(buf, sizeof(buf), "Installing firmware   %d%%", progress);
    centered(buf, y, Font::Body, C_YELLOW);
    gfx->fillRoundRect(24, y + 24, W - 48, 10, 5, C_SURFACE);
    if (progress > 0) gfx->fillRoundRect(24, y + 24, max(10, (W - 48) * progress / 100), 10, 5, C_YELLOW);
    centered("Keep the power on", y + 42, Font::Caption, C_TEXT2);
  } else {
    if (phones > 0) snprintf(buf, sizeof(buf), phones == 1 ? "1 phone connected" : "%d phones connected", phones);
    gfx->fillCircle(16, y + 10, 4, phones > 0 ? C_GREEN : C_TEXT3);
    text(phones > 0 ? buf : "No phone connected yet", 26, y + 10, Font::Body, phones > 0 ? C_TEXT : C_TEXT2, ML_DATUM);
    // Typed on the phone to install firmware or replace the track list
    label("UPLOAD CODE", W - 12, y + 3, TR_DATUM);
    snprintf(buf, sizeof(buf), "%04d", uploadCode);
    text(buf, W - 12, y + 17, Font::Title, C_TEXT, TR_DATUM);
  }

  if (progress < 0 && !restarting) {
    if (touchOk) footerButtons("", "Wi-Fi off", "");
    else centered("Press BOOT to turn Wi-Fi off", 212, Font::Body, C_TEXT3);
  }
  endFrame();
}
