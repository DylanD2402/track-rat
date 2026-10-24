/**
 * Track Rat - Display implementation
 */

#include "DisplayManager.h"
#include "BestLap.h"
#include "Config.h"
#include "SessionLog.h"
#include "Storage.h"
#include "TimingScreen.h"
#include "RaceBoxBle.h"
#include "EngineData.h"
#include "ObdBle.h"
#include "CameraBle.h"
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
  camTagL = camTagR = -1;  // header() sets it again if this screen shows the camera
}

void DisplayManager::cameraAsked(bool wasRecording) {
  camAskedAt = millis();
  if (!camAskedAt) camAskedAt = 1;
  camAskedWasRec = wasRecording;
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

uint16_t DisplayManager::tempColor(float c, bool oil) {
  const tempalert::Limits l = settings::tempLimits(Settings::get());
  switch (oil ? tempalert::oilBand(c, l) : tempalert::coolantBand(c, l)) {
    case tempalert::Band::Cold: return C_BLUE;
    case tempalert::Band::Warm: return C_YELLOW;
    case tempalert::Band::Hot: return C_RED;
    default: return C_TEXT2;
  }
}

int DisplayManager::tempMaxLine(const LapTimer& timer, int right, int y) {
  const float cool = timer.sessionCoolantMaxC(), oil = timer.sessionOilMaxC();
  if (!hasTemp(cool) && !hasTemp(oil)) return 0;
  const bool f = settings::fahrenheit(Settings::get());
  int x = right;
  if (hasTemp(oil)) {
    const String o = String(hasTemp(cool) ? "oil " : "Oil max ") + tempalert::inUnits(oil, f) + "\xC2\xB0";
    x -= text(o, x, y, Font::Caption, tempColor(oil, true), MR_DATUM);
    if (hasTemp(cool)) x -= text("   \xC2\xB7   ", x, y, Font::Caption, C_TEXT3, MR_DATUM);
  }
  if (hasTemp(cool))
    x -= text("Coolant max " + String(tempalert::inUnits(cool, f)) + "\xC2\xB0", x, y, Font::Caption, tempColor(cool, false), MR_DATUM);
  return right - x;
}

// The overheat warning (6.5): COOLANT TOO HOT, the temperature and what to do, black on red or
// yellow. Drawn every frame with TempAlert's phase, so it flashes
void DisplayManager::drawTempAlert(bool oil, float c, bool red) {
  beginFrame();
  const uint16_t bg = red ? C_RED : C_YELLOW;
  gfx->fillScreen(bg);
  const SettingsData s = Settings::get();
  const bool f = settings::fahrenheit(s);
  centered(oil ? "OIL" : "COOLANT", 8, Font::Hero, C_BG);
  centered("TOO HOT", 52, Font::Hero, C_BG);
  // The number in the big digits, and a drawn degree sign (those digits have none)
  const String num = String(tempalert::inUnits(c, f));
  const int w = textWidth(num, Font::NumL), x = (W - w - 18) / 2, y = 102;
  text(num, x, y, Font::NumL, C_BG, TL_DATUM);
  gfx->fillCircle(x + w + 10, y + 9, 7, C_BG);
  gfx->fillCircle(x + w + 10, y + 9, 4, bg);
  centered("Ease off and let it cool", 166, Font::Title, C_BG);
  char buf[48];
  const tempalert::Limits l = settings::tempLimits(s);
  snprintf(buf, sizeof(buf), "%s at %d\xC2\xB0%s   \xC2\xB7   tap to close", oil ? "Oil hot" : "Hot",
           tempalert::inUnits(oil ? l.oilHotAtC : l.hotAtC, f), f ? "F" : "C");
  centered(buf, 206, Font::Body, C_BG);
  endFrame();
}

void DisplayManager::drawSync(const char* gpsTime) {
  beginFrame();
  gfx->fillScreen(TFT_WHITE);
  centered("SYNC", 30, Font::Hero, TFT_BLACK);
  centered(gpsTime, 105, Font::NumL, TFT_BLACK);
  centered("GPS time (UTC)", 190, Font::Body, TFT_BLACK);
  endFrame();
}

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

void DisplayManager::header(const String& title, const GPSHandler* gps, uint16_t titleColor) {
  gfx->drawFastHLine(0, HEADER_H - 1, W, C_LINE);

  // Status, right to left: signal bars, GPS accuracy, battery, BENCH, BT, the coolant (or OBD), CAM
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
    // The engine's coolant temperature (6.5), coloured by Settings > Coolant: blue while cold,
    // yellow when warm, blinking red when hot. OBD while a Bluetooth adapter has none to give.
    float coolC;
    if (EngineData::coolant(millis(), coolC)) {
      const SettingsData s = Settings::get();
      char t[12];
      snprintf(t, sizeof(t), "%d\xC2\xB0", tempalert::inUnits(coolC, settings::fahrenheit(s)));
      switch (tempalert::coolantBand(coolC, settings::tempLimits(s))) {
        case tempalert::Band::Cold: tag(t, C_BLUE, C_TEXT); break;
        case tempalert::Band::Normal: tag(t, C_RAISED, C_TEXT); break;
        case tempalert::Band::Warm: tag(t, C_YELLOW, C_BG); break;
        case tempalert::Band::Hot: {  // blinks slowly like the camera's REC tag
          const bool on = millis() / CAM_REC_BLINK_MS % 2 == 0;
          tag(t, on ? C_RED : rgb(110, 24, 20), on ? C_BG : C_TEXT);
          break;
        }
      }
    } else if (ObdBle::connected()) {
      tag("OBD", C_BLUE, C_TEXT);  // the Bluetooth OBD-II adapter is connected
    }
#if BLE_ENABLED
    if (CameraBle::connected()) {  // the DJI camera: REC while it records, its battery, and a warning when too hot.
      const CameraBle::Status cs = CameraBle::status();  // Tap it to start or stop recording
      const uint32_t now = millis();
      const bool rec = cs.cam.known && cs.cam.recording();
      if (camAskedAt && (rec != camAskedWasRec || now - camAskedAt >= CAM_ASK_SHOW_MS)) camAskedAt = 0;  // it followed (or didn't)
      const int r0 = right;
      char t[16];
      if (cs.cam.known && cs.cam.tempOver >= 2) tag("CAM HOT", C_ORANGE, C_BG);
      else if (camAskedAt) tag(camAskedWasRec ? "STOP..." : "REC...", C_RAISED, C_TEXT);  // asked: waiting for the camera
      else if (rec) {  // recording: blinks slowly, red then dark red
        snprintf(t, sizeof(t), "REC %u%%", cs.cam.battery);
        const bool on = now / CAM_REC_BLINK_MS % 2 == 0;
        tag(t, on ? C_RED : rgb(110, 24, 20), on ? C_BG : C_TEXT);
      } else if (cs.cam.known) {
        snprintf(t, sizeof(t), "CAM %u%%", cs.cam.battery);
        tag(t, C_BLUE, C_TEXT);
      } else {
        tag("CAM", C_BLUE, C_TEXT);
      }
      camTagL = right + 6, camTagR = r0;
    }
#endif
  }

  text(fit(title, Font::Body, right - 12), 10, HEADER_H / 2, Font::Body, titleColor, ML_DATUM);
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
// It waits a little, so an ordinary tap (which switches the view) never flashes it.
void DisplayManager::holdBar(uint32_t holdMs, const char* what) {
  if (holdMs <= HOLD_BAR_DELAY_MS) return;
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

String DisplayManager::pbText() const {
  std::string date = bestlap::dateText(pbDate);
  return "PB " + formatLapTime(pbMs) + (date.empty() ? String("") : String("   ·   ") + date.c_str());
}

void DisplayManager::drawTrackSelect(const Track& track, int index, int count, double distanceM,
                                     const GPSHandler& gps, bool touchOk) {
  beginFrame();
  char buf[40];
  snprintf(buf, sizeof(buf), "‹  Tracks   %d of %d", index + 1, count);  // the top bar goes back to the menu
  header(buf, &gps);

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
  else if (pbMs) centered(pbText(), 168, Font::Body, C_PURPLE);
  else if (touchOk) centered("Tap the top bar for the menu", 170, Font::Caption, C_TEXT3);

  if (touchOk) footerButtons("‹", "Select", "›");
  else centered("BOOT: press for next, hold to select", 212, Font::Body, C_TEXT3);
  endFrame();
}

void DisplayManager::drawTrackConfirm(const Track& track, double distanceM, bool gpsFix, bool touchOk,
                                      int layoutNumber, int layoutCount, bool fromList, bool autoStart) {
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
  if (autoStart) {
    centered("Starts by itself when you drive off", y, Font::Body, C_GREEN);
    if (track.hasStartFinish) {
      centered("Timing begins at the start/finish line", y + 19, Font::Body, C_TEXT);
    } else {
      centered("Then tap the screen as you cross the line", y + 19, Font::Body, C_YELLOW);
    }
  } else if (track.hasStartFinish) {
    centered("Timing starts when you cross", y, Font::Body, C_TEXT);
    centered("the start/finish line", y + 19, Font::Body, C_TEXT);
  } else {
    centered("No start/finish line yet:", y, Font::Body, C_YELLOW);
    centered("tap the screen as you cross it", y + 19, Font::Body, C_TEXT);
  }
  if (pbMs) centered(pbText(), y + 44, Font::Body, C_PURPLE);
  else centered(String("Laps are saved to ") + Storage::sessionFSName(), y + 46, Font::Caption, C_TEXT3);

  if (touchOk) footerButtons(fromList ? "Back" : "Menu", "Start", track.bench ? "" : "Set line");
  else centered(fromList ? "BOOT: hold to start, press to go back" : "BOOT: hold to start, press for menu", 212, Font::Body, C_TEXT3);
  endFrame();
}

// The delta-first timing screen (ROADMAP 2.4; the rules are in TimingScreen.h).
// Top to bottom: the header; the big number on a full-width band, green
// ahead and red behind (or blue and orange) with black digits, and the trend
// bar beside it; two smaller numbers; the sector strip. At the line the whole
// screen flashes the lap time and its gap.
static const int BAND_Y = HEADER_H + 4, BAND_H = 104;
static const int TREND_W = 14, TREND_X = W - 4 - TREND_W;
static const int BAND_W = TREND_X - 6 - 4;
static const int ROW_Y = BAND_Y + BAND_H + 4;  // the two smaller numbers
static const int STRIP_Y = 190, STRIP_H = H - STRIP_Y - 4;

void DisplayManager::drawRunning(const Track& track, const LapTimer& timer, const GPSHandler& gps,
                                 const RunningOverlay& ov, uint32_t holdMs) {
  beginFrame();
  char buf[40];
  header(runningTitle(track, timer), &gps, sessionClockEnding ? C_YELLOW : C_TEXT);
  const DeltaColours pal = Settings::get().deltaColours;

  if (ov.showLap) {  // at the line: the whole screen flashes the lap time and the gap
    const timing::LineFlash fl = timing::lineFlash(ov.lapIsPb, ov.lapIsDayBest);
    const uint16_t bg = timing::flashColour(fl, pal), fg = timing::flashTextColour(fl);
    gfx->fillRect(0, HEADER_H, W, H - HEADER_H, bg);
    snprintf(buf, sizeof(buf), fl == timing::LineFlash::Pb        ? "LAP %d  ·  NEW PB"
                               : fl == timing::LineFlash::DayBest ? "LAP %d  ·  BEST OF THE DAY"
                               : ov.lapIsBest                     ? "LAP %d  ·  SESSION BEST"
                                                                  : "LAP %d",
             ov.lapNumber);
    text(buf, W / 2, HEADER_H + 14, Font::Button, fg, TC_DATUM);
    String t = formatLapTime(ov.lapMs, 3);
    Font f = textWidth(t, Font::NumXL) <= W - 12 ? Font::NumXL : Font::NumL;
    text(t, W / 2, HEADER_H + 46, f, fg, TC_DATUM);
    if (ov.lapDeltaMs != NO_DELTA) {
      String g = formatDelta(ov.lapDeltaMs);
      // On grey the gap takes the delta colour; on a coloured flash it's black like the rest
      text(g, W / 2, HEADER_H + 136, Font::NumL, fl == timing::LineFlash::Lap ? timing::deltaColour(ov.lapDeltaMs, pal) : fg,
           TC_DATUM);
    }
    runningOverlays(gps, holdMs, &ov);
    endFrame();
    return;
  }

  // The Big number setting: what gets the largest digits. Delta and predicted
  // lap need a live delta against a best lap; until then it's the lap time.
  const int32_t liveDelta = gps.hasFix() ? timer.liveDeltaMs() : NO_DELTA;  // without a fix it's stale
  const bool haveDelta = deltaShown(liveDelta, timer.lapRunning()) && timer.referenceLapMs();
  const BigNumber big = settings::bigNumberShown(Settings::get().bigNumber, haveDelta);
  const uint32_t lapMs = timer.currentLapMs(millis());
  const uint32_t predictedMs = haveDelta ? settings::predictedLapMs(timer.referenceLapMs(), liveDelta) : 0;

  // --- the band ---
  const int bandMid = 4 + BAND_W / 2;
  if (!timer.lapRunning()) {
    gfx->fillRoundRect(4, BAND_Y, BAND_W, BAND_H, CORNER, C_SURFACE);
    text("Out lap", bandMid, BAND_Y + 28, Font::Title, C_YELLOW, TC_DATUM);
    text("Cross the start/finish line to begin timing", bandMid, BAND_Y + 64, Font::Caption, C_TEXT2, TC_DATUM);
  } else {
    const uint16_t bg = haveDelta ? timing::deltaColour(liveDelta, pal) : C_SURFACE;
    const uint16_t fg = haveDelta ? C_BG : C_TEXT;
    gfx->fillRoundRect(4, BAND_Y, BAND_W, BAND_H, CORNER, bg);
    String t = big == BigNumber::Delta       ? deltaText(liveDelta)
               : big == BigNumber::Predicted ? formatLapTime(predictedMs, 1)
                                             : formatLapTime(lapMs, 1);
    const bool labelled = big != BigNumber::Delta && haveDelta;  // room above for a label and the sign
    Font f = textWidth(t, Font::NumXL) <= BAND_W - 12 ? Font::NumXL : Font::NumL;
    const int ty = f == Font::NumXL ? BAND_Y + (labelled ? 32 : 16) : BAND_Y + (labelled ? 40 : 30);
    text(t, bandMid, ty, f, fg, TC_DATUM);
    if (labelled) {  // the delta's sign stays big even when it isn't the big number: not colour alone
      text(big == BigNumber::Predicted ? "PREDICTED" : "LAP TIME", 14, BAND_Y + 7, Font::Caption, fg);
      text(deltaText(liveDelta), 4 + BAND_W - 10, BAND_Y + 4, Font::Title, fg, TR_DATUM);
    }
  }

  // --- the trend bar: up for gaining, down for losing ---
  {
    const int top = BAND_Y, h = STRIP_Y - 6 - BAND_Y, mid = top + h / 2;
    gfx->fillRoundRect(TREND_X, top, TREND_W, h, 4, C_SURFACE);
    const float fr = haveDelta ? timing::trendFraction(ov.trendMs) : 0;
    const int len = (int)lroundf(fabsf(fr) * (h / 2 - 2));
    if (len > 0) {
      const uint16_t c = fr > 0 ? timing::colours(pal).ahead : timing::colours(pal).behind;
      if (fr > 0) gfx->fillRect(TREND_X + 2, mid - len, TREND_W - 4, len, c);
      else gfx->fillRect(TREND_X + 2, mid, TREND_W - 4, len, c);
    }
    gfx->drawFastHLine(TREND_X, mid, TREND_W, C_TEXT2);
  }

  // --- two smaller numbers: whichever of lap time, predicted and delta aren't on the band ---
  auto cell = [&](int x, const char* name, const String& value, uint16_t colour) {
    label(name, x, ROW_Y);
    text(value, x, ROW_Y + 15, Font::Title, colour);
  };
  const int cell2 = 4 + BAND_W / 2 + 8;
  gfx->drawFastVLine(cell2 - 8, ROW_Y + 2, 38, C_LINE);
  const Lap* last = timer.lastLap();
  const Lap* best = timer.bestLap();
  if (haveDelta) {
    if (big == BigNumber::LapTime) cell(12, "PREDICTED", formatLapTime(predictedMs, 1), C_TEXT);
    else cell(12, "LAP TIME", formatLapTime(lapMs, 1), C_TEXT);
    if (big == BigNumber::Delta) cell(cell2, "PREDICTED", formatLapTime(predictedMs, 1), C_TEXT);
    else cell(cell2, "BEST", best ? formatLapTime(best->timeMs) : String("–"), best ? C_TEXT : C_TEXT3);
  } else {
    cell(12, "LAST", last ? formatLapTime(last->timeMs) : String("–"), last ? C_TEXT : C_TEXT3);
    cell(cell2, "BEST", best ? formatLapTime(best->timeMs) : String("–"), best ? C_TEXT : C_TEXT3);
  }

  // --- the sector strip: purple best ever, green best of the day, yellow slower ---
  const size_t gates = timer.sectorGates().size();
  if (gates > 0 && timer.lapRunning()) {
    const std::vector<timing::Mark> marks =
        timing::sectorStrip(timer.currentSectorMs(), ov.sectorBestEver, ov.sectorBestDay);
    const int n = (int)marks.size(), gap = n > 12 ? 2 : 4;
    const int bw = (W - 8 - gap * (n - 1)) / n;
    const int x0 = 4 + (W - 8 - (bw * n + gap * (n - 1))) / 2;
    for (int i = 0; i < n; i++) {
      const int x = x0 + i * (bw + gap);
      const uint16_t c = timing::markColour(marks[i], pal);
      const bool done = marks[i] != timing::Mark::Pending;
      gfx->fillRoundRect(x, STRIP_Y, bw, STRIP_H, 6, c);
      if (!done) gfx->drawRoundRect(x, STRIP_Y, bw, STRIP_H, 6, C_LINE);
      snprintf(buf, sizeof(buf), "%d", i + 1);
      if (bw >= 16)
        text(buf, x + bw / 2, STRIP_Y + STRIP_H / 2 + 1, Font::Caption,
             done && marks[i] != timing::Mark::Missed ? C_BG : C_TEXT3, MC_DATUM);
    }
  } else if (haveDelta) {  // no corner sectors (yet): the last lap where the strip goes
    label("LAST", 12, STRIP_Y + 2);
    text(last ? formatLapTime(last->timeMs) : String("–"), 60, STRIP_Y + 2, Font::Body, last ? C_TEXT : C_TEXT3);
  } else {
    centered("Tap: map   ·   Hold: end session", STRIP_Y + 18, Font::Caption, C_TEXT3);
  }

  // For a few seconds after a split: its time and gap, over the strip
  if (ov.splitIndex >= 0) {
    gfx->fillRoundRect(4, STRIP_Y - 2, W - 8, STRIP_H + 2, CORNER, C_RAISED);
    snprintf(buf, sizeof(buf), "S%d  %s", ov.splitIndex + 1, formatLapTime(ov.splitMs).c_str());
    text(buf, 14, STRIP_Y + STRIP_H / 2, Font::Body, C_TEXT, ML_DATUM);
    if (ov.deltaMs != NO_DELTA)
      text(formatDelta(ov.deltaMs), W - 14, STRIP_Y + STRIP_H / 2, Font::Body, timing::deltaColour(ov.deltaMs, pal),
           MR_DATUM);
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

// "Lap 5   12:34": the lap and the session clock, or the lap and the track
// without a clock. Lap number first so a long name gets shortened, not the lap.
String DisplayManager::runningTitle(const Track& track, const LapTimer& timer) {
  const String clock = sessionClock.length() ? sessionClock + "   " : String();
  if (!timer.lapRunning()) return clock + track.displayName();
  char buf[16];
  snprintf(buf, sizeof(buf), "Lap %d   ", timer.currentLapNumber());
  return sessionClock.length() ? buf + sessionClock : buf + track.displayName();
}

void DisplayManager::drawSlow(const Track& track, const LapTimer& timer, const GPSHandler& gps, bool inPitLane,
                              uint32_t holdMs) {
  beginFrame();
  header(runningTitle(track, timer), &gps, sessionClockEnding ? C_YELLOW : C_TEXT);

  const int top = HEADER_H + 4;
  gfx->fillRoundRect(4, top, W - 8, 104, CORNER, C_SURFACE);
  label(inPitLane ? "PIT LANE" : "SLOW", 14, top + 8);
  text(Settings::speedUnit(), W - 14, top + 8, Font::Caption, C_TEXT2, TR_DATUM);
  text(String((long)lroundf(Settings::speed(gps.lastFix().speedKmh))), W / 2, top + 16, Font::NumXL,
       inPitLane ? C_YELLOW : C_TEXT, TC_DATUM);

  const int rowY = top + 112;
  gfx->drawFastVLine(W / 2, rowY + 2, 38, C_LINE);
  const Lap* last = timer.lastLap();
  label("LAST LAP", 12, rowY);
  text(last ? formatLapTime(last->timeMs) : String("–"), 12, rowY + 15, Font::Title, last ? C_TEXT : C_TEXT3);
  label("SESSION", W / 2 + 12, rowY);
  text(sessionClock.length() ? sessionClock : String("–"), W / 2 + 12, rowY + 15, Font::Title,
       sessionClockEnding ? C_YELLOW : C_TEXT);

  char buf[48];
  if (inPitLane) snprintf(buf, sizeof(buf), "Timing comes back out of the pit lane");
  else snprintf(buf, sizeof(buf), "Timing comes back above %.0f %s", Settings::speed(SLOW_SCREEN_KMH), Settings::speedUnit());
  centered(buf, H - 22, Font::Caption, C_TEXT3);

  runningOverlays(gps, holdMs);
  endFrame();
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
                                 MapPath path, bool showCar, geo::XY car, bool carFix, bool showStartFinish,
                                 const std::vector<MapMark>* marks, const geo::XY* ghost) {
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
  auto thickLine = [&](int x0, int y0, int x1, int y1, uint16_t c = C_TRACK) {  // 2 px wide
    gfx->drawLine(x0, y0, x1, y1, c);
    gfx->drawLine(x0 + 1, y0, x1 + 1, y1, c);
    gfx->drawLine(x0, y0 + 1, x1, y1 + 1, c);
    gfx->drawLine(x0 + 1, y0 + 1, x1 + 1, y1 + 1, c);
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

  for (size_t m = 0; marks && m < marks->size(); m++) {  // coloured stretches, each with its number
    const MapMark& mk = (*marks)[m];
    if (mk.to >= path.n || mk.from >= mk.to) continue;
    for (size_t i = mk.from + 1; i <= mk.to; i++)
      thickLine(sx(pt(i - 1)[0]), sy(pt(i - 1)[1]), sx(pt(i)[0]), sy(pt(i)[1]), mk.color);
    const size_t mid = (mk.from + mk.to) / 2;
    const int x = sx(pt(mid)[0]), y = sy(pt(mid)[1]);
    gfx->fillCircle(x, y, 8, mk.color);
    text(String(mk.number), x, y + 1, Font::Caption, C_BG, MC_DATUM);
  }

  if (showStartFinish) {  // a small chequered flag
    int x = sx(0) - 5, y = sy(0) - 5;
    gfx->fillRect(x - 1, y - 1, 12, 12, C_BG);
    for (int i = 0; i < 4; i++)
      gfx->fillRect(x + (i % 2) * 5, y + (i / 2) * 5, 5, 5, ((i % 2) ^ (i / 2)) ? C_BG : C_TEXT);
    gfx->drawRect(x, y, 10, 10, C_TEXT);
  }

  if (ghost) {  // a hollow ring, so the car (solid) reads as the one that's real
    int gx = constrain(sx(ghost->x), bx + 8, bx + bw - 9);
    int gy = constrain(sy(ghost->y), by + 8, by + bh - 9);
    gfx->fillCircle(gx, gy, 8, C_BG);
    gfx->fillCircle(gx, gy, 6, C_TEXT2);
    gfx->fillCircle(gx, gy, 4, C_BG);
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
  header(runningTitle(track, timer), &gps, sessionClockEnding ? C_YELLOW : C_TEXT);

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
  // The ghost: where the reference lap was this far into it. Its path is in metres from the
  // timer's start/finish, the map from the track's reference point.
  geo::XY ghost;
  bool haveGhost = false;
  float gx, gy;
  if (timer.lapRunning() && gps.hasFix() &&
      timing::ghostAt(timer.referencePath(), (int32_t)timer.currentLapMs(millis()), gx, gy)) {
    double la, lo;
    geo::fromLocal(gx, gy, timer.startFinish().lat, timer.startFinish().lon, la, lo);
    ghost = geo::toLocal(la, lo, track.refLat(), track.refLon());
    haveGhost = true;
  }
  bool drawn = drawMapArea(MAP_X, MAP_Y, MAP_W, MAP_H, track.map, mp, gps.lastFix().rxMillis != 0, car,
                           gps.hasFix(), track.hasStartFinish, nullptr, haveGhost ? &ghost : nullptr);
  if (!drawn) {
    const int mx = MAP_X + MAP_W / 2, my = MAP_Y + MAP_H / 2;
    text("No map for this track", mx, my - 20, Font::Body, C_TEXT2, MC_DATUM);
    text("It draws itself once", mx, my + 4, Font::Caption, C_TEXT3, MC_DATUM);
    text("your first lap starts", mx, my + 20, Font::Caption, C_TEXT3, MC_DATUM);
  }

  // --- right-hand panel ---
  int y = MAP_Y;
  const DeltaColours pal = Settings::get().deltaColours;
  if (ov.showLap) {
    const timing::LineFlash fl = timing::lineFlash(ov.lapIsPb, ov.lapIsDayBest);
    uint16_t bg = timing::flashColour(fl, pal), fg = timing::flashTextColour(fl);
    gfx->fillRoundRect(PANEL_X - 4, y, PANEL_W + 4, 46, CORNER, bg);
    char buf[16];
    snprintf(buf, sizeof(buf), fl == timing::LineFlash::Pb ? "LAP %d PB" : ov.lapIsBest ? "LAP %d BEST" : "LAP %d",
             ov.lapNumber);
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
  uint16_t bg = !haveDelta ? C_SURFACE : timing::deltaColour(d, pal), fg = haveDelta ? C_BG : C_TEXT3;
  gfx->fillRoundRect(PANEL_X - 4, y, PANEL_W + 4, 48, CORNER, bg);
  text("LIVE DIFF", PANEL_X + 2, y + 6, Font::Caption, haveDelta ? C_BG : C_TEXT2);
  text(haveDelta ? deltaText(d) : String("–"), PANEL_X + 2, y + 21, Font::Title, fg);

  // For a few seconds after a split: its gap to the best lap's split
  y += 56;
  if (ov.splitIndex >= 0) {
    char buf[24];
    snprintf(buf, sizeof(buf), "S%d %s", ov.splitIndex + 1, ov.deltaMs == NO_DELTA ? "" : formatDelta(ov.deltaMs).c_str());
    gfx->fillRoundRect(PANEL_X - 4, y, PANEL_W + 4, 26, CORNER, C_SURFACE);
    text(buf, PANEL_X + 2, y + 13, Font::Body, ov.deltaMs == NO_DELTA ? C_TEXT : timing::deltaColour(ov.deltaMs, pal),
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

void DisplayManager::drawNewTrack(int highlight, bool touchOk) {
  beginFrame();
  header("New track", nullptr);
  // In the left and right thirds of the screen, where the taps are
  const int bw = W / 3 - 8, by = HEADER_H + 10, bh = 150;
  const int bx[2] = {4, W - 4 - bw};
  struct Item { const char* title; const char* d1; const char* d2; const char* d3; };
  const Item items[2] = {
    {"Circuit", "Laps. Tap the", "screen at the", "start/finish"},
    {"Course", "Autocross, hill", "climb: starts here,", "tap at the finish"},
  };
  for (int i = 0; i < 2; i++) {
    const bool hl = i == highlight;
    gfx->fillRoundRect(bx[i], by, bw, bh, CORNER + 2, hl ? C_RAISED : C_SURFACE);
    if (hl) gfx->drawRoundRect(bx[i], by, bw, bh, CORNER + 2, C_TEXT);
    const int mid = bx[i] + bw / 2;
    text(items[i].title, mid, by + 22, Font::Title, C_TEXT, TC_DATUM);
    text(items[i].d1, mid, by + 70, Font::Caption, C_TEXT2, TC_DATUM);
    text(items[i].d2, mid, by + 86, Font::Caption, C_TEXT2, TC_DATUM);
    text(items[i].d3, mid, by + 102, Font::Caption, C_TEXT2, TC_DATUM);
  }
  text("Back", W / 2, by + bh / 2, Font::Body, C_TEXT3, MC_DATUM);
  centered(touchOk ? "A course is named with today's date" : "BOOT: press for the other, hold to choose", 216,
           Font::Caption, C_TEXT3);
  endFrame();
}

void DisplayManager::drawMenu(int highlight, const GPSHandler& gps, bool noTrackNearby, bool touchOk) {
  beginFrame();
  header(noTrackNearby ? "No track within 5 km" : "Menu", &gps);

  struct Item { const char* title; const char* d1; const char* d2; };
  const Item items[3] = {
    {"Free Roam", "Map your drive,", "time and speed"},
    {"New Track", "A circuit, or an", "autocross course"},
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

void DisplayManager::drawSummary(const Track& track, const LapTimer& timer, const String& savedPath, int session,
                                 bool inPits) {
  beginFrame();
  String title = session > 0 ? "Session " + String(session) : String(inPits ? "" : "Session summary");
  if (inPits) title = title.length() ? "Pits   ·   " + title : String("In the pits");
  header(title + "   ·   " + track.displayName(), nullptr);

  char buf[48];
  const Lap* best = timer.bestLap();
  const uint32_t avg = timer.averageLapMs();  // of the untagged laps
  // Laps, best and average across the top
  int y = HEADER_H + 6;
  label("LAPS", 12, y);
  label("BEST", 76, y);
  label("AVERAGE", 196, y);
  text(String((int)timer.lapCount()), 12, y + 14, Font::Title, C_TEXT);
  text(best ? formatLapTime(best->timeMs) : String("–"), 76, y + 14, Font::Title, best ? C_GREEN : C_TEXT3);
  text(avg ? formatLapTime(avg) : String("–"), 196, y + 14, Font::Title, avg ? C_TEXT : C_TEXT3);

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
    else if (l.tags) text(laptags::shortLabel(l.tags), 168, y, Font::Caption, laptags::suspect(l.tags) ? C_ORANGE : C_YELLOW, ML_DATUM);
    snprintf(buf, sizeof(buf), "%.0f %s", Settings::speed(l.maxSpeedKmh), Settings::speedUnit());  // top speed
    text(buf, W - 20, y, Font::Body, C_TEXT3, MR_DATUM);
    if (rows < maxRows - 1 && i > 0) gfx->drawFastHLine(20, y + rowH / 2, W - 40, C_LINE);
    y += rowH;
  }
  if (timer.lapCount() == 0) text("No complete laps recorded", W / 2, ly + (rowH * maxRows + 8) / 2, Font::Body, C_TEXT3, MC_DATUM);

  String saved = savedPath.length() ? "Saved to " + String(Storage::sessionFSName()) + ": " + savedPath : String("Not saved");
  const int temps = tempMaxLine(timer, W - 12, 186);  // the engine's hottest, when it sent temperatures
  text(fit(saved, Font::Caption, W - 24 - (temps ? temps + 12 : 0)), 12, 186, Font::Caption, C_TEXT3, ML_DATUM);
  if (inPits) {  // still in the session: no buttons to bump
    centered("Drive off to carry on", WIFI_BUTTON_Y + 8, Font::Body, C_TEXT);
    centered("Tap: timing screen   ·   Hold: end the session", WIFI_BUTTON_Y + 30, Font::Caption, C_TEXT3);
    endFrame();
    return;
  }
  // Menu and Again in the left and centre thirds, Wi-Fi in the right (the taps are in main.cpp)
  const int bw = W / 3, bh = H - WIFI_BUTTON_Y - 4;
  button(4, WIFI_BUTTON_Y, bw - 8, bh, "Menu", false);
  button(bw + 4, WIFI_BUTTON_Y, bw - 8, bh, "Again", true);
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
