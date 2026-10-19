/**
 * Track Rat - Display implementation
 */

#include "DisplayManager.h"
#include "Config.h"
#include "SessionLog.h"
#include "Storage.h"
#include "RaceBoxBle.h"

static const int W = 320;
static const int H = 240;
static const int HEADER_H = 26;
static const int FOOTER_Y = 202;

static const uint16_t C_BG = TFT_BLACK;
static const uint16_t C_TEXT = TFT_WHITE;
static const uint16_t C_DIM = TFT_DARKGREY;
static const uint16_t C_HEADER = 0x10A2;  // dark slate
static const uint16_t C_GOOD = TFT_GREEN;
static const uint16_t C_BAD = TFT_RED;
static const uint16_t C_WARN = TFT_YELLOW;
static const uint16_t C_BUTTON = 0x2124;

bool DisplayManager::begin() {
  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.invertDisplay(DISPLAY_INVERT);
  tft.fillScreen(C_BG);
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  spr.setColorDepth(16);
  useSprite = spr.createSprite(W, H) != nullptr;  // ~150 KB, lands in PSRAM
  if (useSprite) {
    // Copy of what is currently on the panel, so endFrame() can send only changed rows
    shown = (uint16_t*)ps_malloc(W * H * sizeof(uint16_t));
    shownValid = false;
  }
  gfx = useSprite ? (TFT_eSPI*)&spr : &tft;
  DEBUG_PRINTF("Display: %dx%d, %s\n", tft.width(), tft.height(),
               useSprite ? "sprite buffer in PSRAM" : "NO sprite (PSRAM missing?) - direct draw");

#if DISPLAY_TEST_PATTERN
  // Four vertical stripes, each a quarter of the width. If one is missing or
  // shows something else, that part of the panel isn't being written.
  const uint16_t stripe[4] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE};
  const char* names[4] = {"RED", "GREEN", "BLUE", "WHITE"};
  for (int i = 0; i < 4; i++) {
    gfx->fillRect(i * W / 4, 0, W / 4, H, stripe[i]);
    gfx->setTextDatum(MC_DATUM);
    gfx->setTextColor(i == 3 ? TFT_BLACK : TFT_WHITE);
    gfx->drawString(names[i], i * W / 4 + W / 8, H / 2, 2);
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

void DisplayManager::centered(const String& s, int y, int font, uint16_t color) {
  if (font == 4 && gfx->textWidth(s, 4) > W - 8) font = 2;  // long names: smaller font
  gfx->setTextDatum(TC_DATUM);
  gfx->setTextColor(color);
  gfx->drawString(s, W / 2, y, font);
  gfx->setTextDatum(TL_DATUM);
}

// Phone-style signal bars. Green = fix, yellow = searching, red outline = no data.
void DisplayManager::gpsBadge(const GPSHandler& gps) {
  const int BARS = 5, BAR_W = 5, GAP = 2, MAX_H = 18;
  const int x0 = W - 6 - BARS * BAR_W - (BARS - 1) * GAP;
  const int baseY = HEADER_H - 4;

  int sats = gps.satellites();
  int lit = !gps.receivingData() ? 0 : sats >= 10 ? 5 : sats >= 8 ? 4 : sats >= 6 ? 3 : sats >= 4 ? 2 : sats >= 1 ? 1 : 0;
  uint16_t on = gps.hasFix() ? C_GOOD : C_WARN;
  uint16_t off = gps.receivingData() ? C_DIM : C_BAD;

  for (int i = 0; i < BARS; i++) {
    int h = MAX_H * (i + 1) / BARS;
    int x = x0 + i * (BAR_W + GAP);
    if (i < lit) gfx->fillRect(x, baseY - h, BAR_W, h, on);
    else gfx->drawRect(x, baseY - h, BAR_W, h, off);
  }
}

// Font 2 has no ± sign, so text is drawn with '#' standing for one.
// Returns the x just past the text.
static int drawPmText(TFT_eSPI* g, const char* s, int x, int y, uint16_t color) {
  g->setTextColor(color);
  char seg[48];
  while (*s) {
    if (*s == '#') {
      g->drawFastVLine(x + 3, y + 3, 7, color);   // +
      g->drawFastHLine(x, y + 6, 7, color);
      g->drawFastHLine(x, y + 11, 7, color);      // -
      x += 8;
      s++;
      continue;
    }
    size_t n = 0;
    while (s[n] && s[n] != '#' && n < sizeof(seg) - 1) n++;
    memcpy(seg, s, n);
    seg[n] = 0;
    x += g->drawString(seg, x, y, 2);
    s += n;
  }
  return x;
}

static int pmTextWidth(TFT_eSPI* g, const char* s) {
  int w = 0;
  char rest[48];
  size_t n = 0;
  for (; *s; s++) {
    if (*s == '#') w += 8;
    else if (n < sizeof(rest) - 1) rest[n++] = *s;
  }
  rest[n] = 0;
  return w + g->textWidth(rest, 2);
}

// GPS accuracy colour: green to 2 m, yellow to 5 m, red beyond
static uint16_t accuracyColor(float m) { return m <= 2.0f ? C_GOOD : m <= 5.0f ? C_WARN : C_BAD; }

static void accuracyText(char* buf, size_t len, float m, const char* space) {
  if (m < 10) snprintf(buf, len, "#%.1f%sm", m, space);
  else snprintf(buf, len, "#%.0f%sm", m < 99 ? m : 99, space);
}

void DisplayManager::header(const String& title, const GPSHandler* gps) {
  gfx->fillRect(0, 0, W, HEADER_H, C_HEADER);

  // Status tags right to left: signal bars, battery, BENCH, BT
  int right = W;
  auto tag = [&](const char* text, int w, uint16_t bg, uint16_t fg) {
    right -= w + 4;
    gfx->fillRoundRect(right, 4, w, HEADER_H - 8, 4, bg);
    gfx->setTextDatum(MC_DATUM);
    gfx->setTextColor(fg);
    gfx->drawString(text, right + w / 2, HEADER_H / 2, 2);
    gfx->setTextDatum(TL_DATUM);
  };
  if (gps) {
    gpsBadge(*gps);
    right = W - 44;
    if (gps->hasFix() && gps->accuracyM() >= 0) {  // u-blox: GPS accuracy beside the bars
      char acc[16];
      accuracyText(acc, sizeof(acc), gps->accuracyM(), "");
      right -= pmTextWidth(gfx, acc) + 4;
      drawPmText(gfx, acc, right, 5, accuracyColor(gps->accuracyM()));
    }
  }
  if (batteryPct >= 0) {
    char b[8];
    snprintf(b, sizeof(b), "%d%%", batteryPct);
    tag(b, 40, C_BUTTON, batteryPct <= 15 ? C_BAD : batteryPct <= 30 ? C_WARN : C_TEXT);
  }
  if (gps) {
    if (gps->isSimulated()) tag("BENCH", 48, C_WARN, C_BG);  // simulated timing must be obvious
#if BLE_ENABLED
    if (RaceBoxBle::connected()) tag("BT", 26, 0x041F, C_TEXT);  // a phone app is receiving GPS
#endif
  }

  gfx->setTextColor(C_TEXT);
  String t = title;
  int maxW = right - 12;
  while (t.length() > 3 && gfx->textWidth(t, 2) > maxW) t.remove(t.length() - 1);
  gfx->drawString(t, 6, 5, 2);
}

void DisplayManager::footerButtons(const char* left, const char* center, const char* right) {
  const int bw = W / 3;
  const char* labels[3] = {left, center, right};
  for (int i = 0; i < 3; i++) {
    if (!labels[i] || !labels[i][0]) continue;
    int x = i * bw + 4;
    gfx->fillRoundRect(x, FOOTER_Y, bw - 8, H - FOOTER_Y - 4, 6, i == 1 ? 0x03E0 : C_BUTTON);
    gfx->setTextDatum(MC_DATUM);
    gfx->setTextColor(C_TEXT);
    gfx->drawString(labels[i], x + (bw - 8) / 2, FOOTER_Y + (H - FOOTER_Y - 4) / 2, 4);
  }
  gfx->setTextDatum(TL_DATUM);
}

String DisplayManager::distanceText(double m) {
  if (m < 0) return String("");
  char buf[24];
  if (m < 1000) snprintf(buf, sizeof(buf), "%.0f m", m);
  else if (m < 100000) snprintf(buf, sizeof(buf), "%.1f km", m / 1000.0);
  else snprintf(buf, sizeof(buf), "%.0f km", m / 1000.0);
  return String(buf);
}

// ---------------------------------------------------------------------------

void DisplayManager::drawBoot(const char* status) {
  if (splashRunning()) return;  // the logo animation owns the screen
  beginFrame();
  centered("TRACK RAT", 60, 4, C_TEXT);
  centered("GPS lap timer", 95, 2, C_TEXT);
  centered(status, 160, 2, C_DIM);
  endFrame();
}

void DisplayManager::drawGpsWait(const GPSHandler& gps, int trackCount, const char* trackSource,
                                 bool touchOk, bool statusOnly) {
  beginFrame();
  header(statusOnly ? "GPS STATUS" : "TRACK RAT - waiting for GPS", &gps);

  char buf[64];
  int y = 36;
  gfx->setTextColor(C_DIM);
  gfx->drawString("Data", 10, y, 2);
  if (gps.receivingData()) {
    if (gps.isSimulated()) snprintf(buf, sizeof(buf), "SIMULATED - no GPS detected");
    else snprintf(buf, sizeof(buf), "%s OK @ %lu, GPIO %d", gps.usingUbx() ? "UBX" : "NMEA",
                  (unsigned long)gps.baud(), gps.rxPinUsed());
    gfx->setTextColor(C_GOOD);
  } else {
    snprintf(buf, sizeof(buf), "NO DATA - check GPS wiring, press RST");
    gfx->setTextColor(C_BAD);
  }
  gfx->drawString(buf, 90, y, 2);

  y += 24;
  gfx->setTextColor(C_DIM);
  gfx->drawString("Fix", 10, y, 2);
  gfx->setTextColor(gps.hasFix() ? C_GOOD : C_WARN);
  int x = 90 + gfx->drawString(gps.hasFix() ? "LOCKED" : "searching...", 90, y, 2);
  const GpsFix& lf = gps.lastFix();
  if (gps.hasFix() && lf.accuracyKnown()) {  // u-blox: position and speed accuracy
    char acc[16];
    accuracyText(acc, sizeof(acc), lf.hAccM, " ");
    x = drawPmText(gfx, "   ", x, y, C_TEXT);
    x = drawPmText(gfx, acc, x, y, accuracyColor(lf.hAccM));
    snprintf(buf, sizeof(buf), "   #%.1f km/h   %s", lf.speedAccMs * 3.6f, lf.fixType == 2 ? "2D" : "3D");
    drawPmText(gfx, buf, x, y, C_TEXT);
  }

  y += 24;
  gfx->setTextColor(C_DIM);
  gfx->drawString("Satellites", 10, y, 2);
  snprintf(buf, sizeof(buf), "%d   %s %.1f   %.1f Hz", gps.satellites(), gps.usingUbx() ? "PDOP" : "HDOP",
           gps.hdop(), gps.fixRateHz());
  gfx->setTextColor(C_TEXT);
  gfx->drawString(buf, 90, y, 2);

  y += 24;
  gfx->setTextColor(C_DIM);
  gfx->drawString("Position", 10, y, 2);
  if (gps.lastFix().rxMillis != 0) {
    snprintf(buf, sizeof(buf), "%.6f, %.6f", gps.lastFix().lat, gps.lastFix().lon);
    gfx->setTextColor(C_TEXT);
    gfx->drawString(buf, 90, y, 2);
  }

  y += 24;
  gfx->setTextColor(C_DIM);
  gfx->drawString("Tracks", 10, y, 2);
  snprintf(buf, sizeof(buf), "%d (%s)   Storage: %s", trackCount, trackSource, Storage::sessionFSName());
  gfx->setTextColor(C_TEXT);
  gfx->drawString(buf, 90, y, 2);

  // satellite bar
  int bars = min((int)gps.satellites(), 12);
  for (int i = 0; i < 12; i++) {
    gfx->fillRect(10 + i * 25, 170, 20, 12, i < bars ? (gps.hasFix() ? C_GOOD : C_WARN) : C_BUTTON);
  }

  if (statusOnly) centered(touchOk ? "Tap to go back" : "Press BOOT to go back", 210, 2, C_DIM);
  else centered(touchOk ? "Tap the screen (or BOOT) for the menu" : "Press BOOT for the menu", 210, 2, C_DIM);
  endFrame();
}

void DisplayManager::drawTrackSelect(const Track& track, int index, int count, double distanceM,
                                     const GPSHandler& gps, bool touchOk) {
  beginFrame();
  char buf[32];
  snprintf(buf, sizeof(buf), "SELECT TRACK   %d / %d", index + 1, count);
  header(buf, &gps);  // tap the top bar for GPS status

  centered(track.name, 44, 4, C_TEXT);
  centered(track.location + (track.country.length() ? ", " + track.country : String("")), 82, 2, C_TEXT);
  if (track.length > 0) {
    snprintf(buf, sizeof(buf), "%.2f km", track.length / 1000.0);
    centered(track.layoutName.length() ? track.layoutName + "  " + buf : String(buf), 108, 4, C_TEXT);
  } else if (track.layoutName.length()) {
    centered(track.layoutName, 108, 4, C_TEXT);
  }
  if (distanceM >= 0) centered(distanceText(distanceM) + " from you", 146, 2, C_DIM);
  if (!track.hasStartFinish && !track.bench) {
    centered("no start/finish line yet - set it on track", 166, 2, C_WARN);
  } else if (!track.splits.empty()) {
    snprintf(buf, sizeof(buf), "%d split%s", (int)track.splits.size(), track.splits.size() > 1 ? "s" : "");
    centered(buf, 168, 2, C_DIM);
  }

  if (touchOk) {
    centered("tap the top bar for GPS status", 188, 1, C_DIM);
    footerButtons("<", "SELECT", ">");
  } else {
    centered("BOOT: tap = next, hold = select", 212, 2, C_DIM);
  }
  endFrame();
}

void DisplayManager::drawTrackConfirm(const Track& track, double distanceM, bool gpsFix, bool touchOk,
                                      int layoutNumber, int layoutCount) {
  beginFrame();
  char buf[48];
  if (layoutCount > 1 && touchOk) {
    snprintf(buf, sizeof(buf), "READY   layout %d/%d - tap here for next", layoutNumber + 1, layoutCount);
    header(buf, nullptr);
  } else {
    header("READY", nullptr);
  }

  centered(track.name, 40, 4, C_TEXT);
  if (track.layoutName.length()) centered(track.layoutName + " layout", 66, 2, C_GOOD);
  const char* what = track.hasStartFinish ? "Start/finish" : "Track";
  if (track.bench) {
    centered("Simulated car - no GPS needed", 84, 2, C_WARN);
  } else if (gpsFix && distanceM >= 0) {
    centered(String(what) + " is " + distanceText(distanceM) + " away", 84, 2, distanceM < 2000 ? C_GOOD : C_WARN);
  } else {
    centered("No GPS fix yet", 84, 2, C_WARN);
  }
  if (track.hasStartFinish) {
    centered("Timing starts when you cross", 116, 2, C_TEXT);
    centered("the start/finish line", 134, 2, C_TEXT);
  } else {
    centered("No start/finish line yet:", 116, 2, C_WARN);
    centered("tap the screen as you cross it", 134, 2, C_TEXT);
  }
  centered(String("Laps are saved to ") + Storage::sessionFSName(), 160, 2, C_DIM);
  if (!track.bench && track.hasStartFinish) centered("SET LINE: mark a new start/finish", 182, 1, C_DIM);

  if (touchOk) footerButtons("BACK", "START", track.bench ? "" : "SET LINE");
  else centered("BOOT: hold = start, tap = back", 212, 2, C_DIM);
  endFrame();
}

void DisplayManager::drawRunning(const Track& track, const LapTimer& timer, const GPSHandler& gps,
                                 const RunningOverlay& ov, uint32_t holdMs) {
  beginFrame();
  char buf[40];
  header(runningTitle(track, timer), &gps);

  // --- big time area (y 28..106) ---
  if (ov.showLap) {
    uint16_t bg = ov.lapIsBest ? 0x0320 : 0x0010;
    gfx->fillRect(0, HEADER_H + 2, W, 78, bg);
    snprintf(buf, sizeof(buf), ov.lapIsBest ? "LAP %d  -  BEST LAP" : "LAP %d", ov.lapNumber);
    gfx->setTextDatum(TC_DATUM);
    gfx->setTextColor(C_TEXT);
    gfx->drawString(buf, W / 2, HEADER_H + 5, 2);
    gfx->drawString(formatLapTime(ov.lapMs, 3), W / 2, HEADER_H + 26, 7);
    gfx->setTextDatum(TL_DATUM);
  } else if (timer.lapRunning()) {
    String t = formatLapTime(timer.currentLapMs(millis()), 1);
    int font = gfx->textWidth(t, 8) <= W - 10 ? 8 : 7;
    centered(t, font == 8 ? HEADER_H + 4 : HEADER_H + 18, font, C_TEXT);
  } else {
    centered("OUT LAP", HEADER_H + 16, 4, C_WARN);
    centered("Cross start/finish to begin timing", HEADER_H + 52, 2, C_DIM);
  }

  // --- last / best (y 108..150) ---
  const int rowY = 108;
  gfx->setTextColor(C_DIM);
  gfx->drawString("LAST", 10, rowY, 2);
  gfx->drawString("BEST", W / 2 + 10, rowY, 2);
  const Lap* last = timer.lastLap();
  const Lap* best = timer.bestLap();
  gfx->setTextColor(C_TEXT);
  gfx->drawString(last ? formatLapTime(last->timeMs) : String("-:--.---"), 10, rowY + 16, 4);
  gfx->setTextColor(best ? C_GOOD : C_TEXT);
  gfx->drawString(best ? formatLapTime(best->timeMs) : String("-:--.---"), W / 2 + 10, rowY + 16, 4);

  // --- live diff box (y 156..236) ---
  diffBox(timer.liveDeltaMs(), timer.lapRunning(), ov);

  runningOverlays(gps, holdMs, &ov);
  endFrame();
}

void DisplayManager::runningOverlays(const GPSHandler& gps, uint32_t holdMs, const RunningOverlay* ov) {
  if (!gps.hasFix()) {
    gfx->fillRect(0, H - 30, W, 30, C_BAD);
    centered("NO GPS FIX", H - 26, 4, C_TEXT);
  }

  if (ov && ov->notice.length() && holdMs <= 150) {
    gfx->fillRoundRect(10, 148, W - 20, 62, 8, C_WARN);  // below the lap-time flash
    gfx->setTextColor(TFT_BLACK);
    gfx->setTextDatum(TC_DATUM);
    gfx->drawString(ov->notice, W / 2, 154, 4);
    gfx->drawString("matched your lap - switched", W / 2, 186, 2);
    gfx->setTextDatum(TL_DATUM);
  }

  if (holdMs > 150) {
    int w = (int)min((uint32_t)(W - 40), (uint32_t)((W - 40) * holdMs / END_SESSION_HOLD_MS));
    gfx->fillRect(10, 90, W - 20, 50, C_HEADER);
    centered("Keep holding to end session", 96, 2, C_TEXT);
    gfx->fillRect(20, 118, w, 12, C_WARN);
  }
}

String DisplayManager::runningTitle(const Track& track, const LapTimer& timer) {
  if (!timer.lapRunning()) return track.displayName();
  char buf[16];
  snprintf(buf, sizeof(buf), "LAP %d   ", timer.currentLapNumber());
  return buf + track.displayName();  // lap number first so a long name gets truncated, not the lap
}

// ---------------------------------------------------------------------------
// Map view: track layout on the left, timing on the right.

static const int MAP_X = 2, MAP_Y = HEADER_H + 2, MAP_W = 212, MAP_H = H - HEADER_H - 4;
static const int PANEL_X = MAP_X + MAP_W + 6;
static const uint16_t C_TRACK = 0xBDF7;  // light grey

// Draws stored lines and/or a path (points whose first two members are float
// x, y in metres) scaled to fit the box, north up, plus the start/finish
// marker at the origin and the car. Returns false if there was nothing to draw.
bool DisplayManager::drawMapArea(int bx, int by, int bw, int bh, const std::vector<std::vector<geo::XYf>>& lines,
                                 MapPath path, bool showCar, geo::XY car, bool carFix, bool showStartFinish) {
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

  const float pad = 10;
  float sw = max(maxX - minX, 1.0f), sh = max(maxY - minY, 1.0f);
  float scale = min((bw - 2 * pad) / sw, (bh - 2 * pad) / sh);
  float cx = bx + bw / 2.0f, cy = by + bh / 2.0f;
  float midX = (minX + maxX) / 2, midY = (minY + maxY) / 2;
  auto sx = [&](float x) { return (int)lroundf(cx + (x - midX) * scale); };
  auto sy = [&](float y) { return (int)lroundf(cy - (y - midY) * scale); };  // north up
  auto thickLine = [&](int x0, int y0, int x1, int y1) {
    gfx->drawLine(x0, y0, x1, y1, C_TRACK);
    gfx->drawLine(x0 + 1, y0, x1 + 1, y1, C_TRACK);
    gfx->drawLine(x0, y0 + 1, x1, y1 + 1, C_TRACK);
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

  if (showStartFinish) {
    gfx->fillRect(sx(0) - 4, sy(0) - 4, 9, 9, C_TEXT);
    gfx->drawRect(sx(0) - 4, sy(0) - 4, 9, 9, C_BG);
  }

  if (showCar) {  // kept inside the box if it's off the drawn area
    int px = constrain(sx(car.x), bx + 7, bx + bw - 8);
    int py = constrain(sy(car.y), by + 7, by + bh - 8);
    gfx->fillCircle(px, py, 8, C_BG);  // dark ring keeps it visible on the grey track line
    gfx->fillCircle(px, py, 6, carFix ? C_TEXT : C_WARN);
  }
  return true;
}

void DisplayManager::drawRunningMap(const Track& track, const LapTimer& timer, const GPSHandler& gps,
                                    const RunningOverlay& ov, uint32_t holdMs) {
  beginFrame();
  header(runningTitle(track, timer), &gps);
  gfx->drawRoundRect(MAP_X, MAP_Y, MAP_W, MAP_H, 6, C_BUTTON);

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
    gfx->setTextDatum(MC_DATUM);
    gfx->setTextColor(C_DIM);
    gfx->drawString("No map for this track", MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2 - 10, 2);
    gfx->drawString("It draws itself once", MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2 + 10, 2);
    gfx->drawString("your first lap starts", MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2 + 28, 2);
    gfx->setTextDatum(TL_DATUM);
  }

  // --- right-hand panel ---
  const int pw = W - PANEL_X - 2;
  int y = MAP_Y + 2;
  if (ov.showLap) {
    gfx->fillRoundRect(PANEL_X - 2, y - 2, pw + 2, 44, 5, ov.lapIsBest ? 0x0320 : 0x0010);
    char buf[16];
    snprintf(buf, sizeof(buf), ov.lapIsBest ? "LAP %d BEST" : "LAP %d", ov.lapNumber);
    gfx->setTextColor(C_TEXT);
    gfx->drawString(buf, PANEL_X, y, 2);
    gfx->drawString(formatLapTime(ov.lapMs, 1), PANEL_X, y + 16, 4);
  } else {
    gfx->setTextColor(C_DIM);
    gfx->drawString(timer.lapRunning() ? "LAP TIME" : "OUT LAP", PANEL_X, y, 2);
    gfx->setTextColor(C_TEXT);
    gfx->drawString(timer.lapRunning() ? formatLapTime(timer.currentLapMs(millis()), 1) : String("--"),
                    PANEL_X, y + 16, 4);
  }

  y += 52;
  int32_t d = timer.liveDeltaMs();
  bool haveDelta = timer.lapRunning() && d != NO_DELTA && d > -100000 && d < 100000;
  gfx->fillRoundRect(PANEL_X - 2, y - 2, pw + 2, 46, 5, !haveDelta ? C_BUTTON : (d <= 0 ? 0x0360 : 0xA000));
  gfx->setTextColor(haveDelta ? C_TEXT : C_DIM);
  gfx->drawString("LIVE DIFF", PANEL_X, y, 2);
  if (haveDelta) {
    uint32_t a = d < 0 ? -d : d;
    char buf[16];
    snprintf(buf, sizeof(buf), "%c%lu.%02lu", d <= 0 ? '-' : '+', (unsigned long)(a / 1000),
             (unsigned long)((a % 1000) / 10));
    gfx->drawString(buf, PANEL_X, y + 17, 4);
  } else {
    gfx->drawString("--", PANEL_X, y + 17, 4);
  }

  y += 54;
  if (ov.splitIndex >= 0) {
    char buf[24];
    snprintf(buf, sizeof(buf), "S%d %s", ov.splitIndex + 1, ov.deltaMs == NO_DELTA ? "" : formatDelta(ov.deltaMs).c_str());
    gfx->setTextColor(ov.deltaMs == NO_DELTA ? C_TEXT : (ov.deltaMs <= 0 ? C_GOOD : C_BAD));
    gfx->drawString(buf, PANEL_X, y, 2);
  }

  y += 22;
  const Lap* best = timer.bestLap();
  const Lap* last = timer.lastLap();
  gfx->setTextColor(C_DIM);
  gfx->drawString("BEST", PANEL_X, y, 2);
  gfx->setTextColor(best ? C_GOOD : C_DIM);
  gfx->drawString(best ? formatLapTime(best->timeMs) : String("-:--.---"), PANEL_X, y + 15, 2);
  y += 36;
  gfx->setTextColor(C_DIM);
  gfx->drawString("LAST", PANEL_X, y, 2);
  gfx->setTextColor(last ? C_TEXT : C_DIM);
  gfx->drawString(last ? formatLapTime(last->timeMs) : String("-:--.---"), PANEL_X, y + 15, 2);

  gfx->setTextColor(C_DIM);
  gfx->drawString("tap: timing", PANEL_X, H - 12, 1);

  runningOverlays(gps, holdMs, &ov);
  endFrame();
}

// Live gap to the best lap: green box = ahead, red = behind.
void DisplayManager::diffBox(int32_t delta, bool lapRunning, const RunningOverlay& ov) {
  const int x = 4, y = 156, w = W - 8, h = H - 156 - 4;
  bool have = lapRunning && delta != NO_DELTA && delta > -100000 && delta < 100000;
  uint16_t bg = !have ? C_BUTTON : (delta <= 0 ? 0x0360 : 0xA000);
  gfx->fillRoundRect(x, y, w, h, 8, bg);

  gfx->setTextColor(have ? C_TEXT : C_DIM);
  gfx->drawString("LIVE DIFF", x + 8, y + 5, 2);

  // Split result for a few seconds after each split
  if (ov.splitIndex >= 0) {
    char sbuf[40];
    snprintf(sbuf, sizeof(sbuf), "S%d %s  %s", ov.splitIndex + 1, formatLapTime(ov.splitMs).c_str(),
             ov.deltaMs == NO_DELTA ? "" : formatDelta(ov.deltaMs).c_str());
    gfx->setTextDatum(TR_DATUM);
    gfx->setTextColor(C_TEXT);
    gfx->drawString(sbuf, x + w - 8, y + 5, 2);
    gfx->setTextDatum(TL_DATUM);
  }

  if (!have) {
    gfx->setTextDatum(MC_DATUM);
    gfx->setTextColor(C_DIM);
    gfx->drawString(lapRunning ? "--" : "", W / 2, y + h / 2 + 6, 4);
    gfx->drawString("tap: map   hold: end session", W / 2, y + h - 10, 1);
    gfx->setTextDatum(TL_DATUM);
    return;
  }

  // Font 6 (48 px digits) has '-' but no '+', so the plus is drawn by hand.
  uint32_t a = delta < 0 ? -delta : delta;
  char num[16];
  snprintf(num, sizeof(num), "%s%lu.%02lu", delta < 0 ? "-" : "", (unsigned long)(a / 1000),
           (unsigned long)((a % 1000) / 10));
  const int PLUS = 28, GAP = 6;
  int tw = gfx->textWidth(num, 6);
  int total = tw + (delta > 0 ? PLUS + GAP : 0);
  int tx = (W - total) / 2;
  int ty = y + 24;
  if (delta > 0) {
    int cy = ty + 24;
    gfx->fillRect(tx, cy - 3, PLUS, 6, C_TEXT);
    gfx->fillRect(tx + PLUS / 2 - 3, cy - PLUS / 2, 6, PLUS, C_TEXT);
    tx += PLUS + GAP;
  }
  gfx->setTextColor(C_TEXT);
  gfx->drawString(num, tx, ty, 6);
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

void DisplayManager::drawMenu(int highlight, const GPSHandler& gps, bool noTrackNearby, bool touchOk) {
  beginFrame();
  header(noTrackNearby ? "NO TRACK WITHIN 5 KM" : "MENU", &gps);

  struct Item { const char* l1; const char* l2; const char* d1; const char* d2; };
  const Item items[3] = {
    {"FREE", "ROAM", "Map your drive,", "time and speed"},
    {"NEW", "TRACK", "Tap the screen at", "the start/finish"},
    {"TRACK", "LIST", "Pick a track or", "Bench Test"},
  };
  const int bw = W / 3, top = HEADER_H + 8, bh = 114;  // the WI-FI button is below, y 154..186
  for (int i = 0; i < 3; i++) {
    int x = i * bw + 4;
    bool hl = (i == highlight);
    gfx->fillRoundRect(x, top, bw - 8, bh, 8, hl ? 0x3186 : C_BUTTON);
    if (hl) gfx->drawRoundRect(x, top, bw - 8, bh, 8, C_TEXT);
    gfx->setTextDatum(MC_DATUM);
    gfx->setTextColor(C_TEXT);
    gfx->drawString(items[i].l1, x + (bw - 8) / 2, top + 30, 4);
    gfx->drawString(items[i].l2, x + (bw - 8) / 2, top + 56, 4);
    gfx->setTextColor(C_DIM);
    gfx->drawString(items[i].d1, x + (bw - 8) / 2, top + 88, 1);
    gfx->drawString(items[i].d2, x + (bw - 8) / 2, top + 100, 1);
  }
  const int py = MENU_WIFI_Y, ph = 32;
  gfx->fillRoundRect(4, py, 2 * bw - 8, ph, 8, highlight == 3 ? 0x3186 : C_BUTTON);
  if (highlight == 3) gfx->drawRoundRect(4, py, 2 * bw - 8, ph, 8, C_TEXT);
  gfx->setTextDatum(MC_DATUM);
  gfx->setTextColor(C_TEXT);
  gfx->drawString("PERFORMANCE", bw, py + 16, 4);
  wifiButton(MENU_WIFI_Y, 32, highlight == 4);
  gfx->setTextDatum(TL_DATUM);
  wifiButton(MENU_WIFI_Y, 32, highlight == 3);
  if (!gps.hasFix()) centered("Free Roam and New Track need a GPS fix", 192, 2, C_WARN);
  centered(touchOk ? "tap a mode   -   top bar: GPS status" : "BOOT: tap = next, hold = choose", 218, 2, C_DIM);
  endFrame();
}

void DisplayManager::drawFreeRoam(const FreeRoam& fr, const GPSHandler& gps, uint32_t holdMs) {
  beginFrame();
  header("FREE ROAM", &gps);
  gfx->drawRoundRect(MAP_X, MAP_Y, MAP_W, MAP_H, 6, C_BUTTON);

  static const std::vector<std::vector<geo::XYf>> noLines;
  MapPath mp = {(const uint8_t*)fr.path.data(), fr.path.size(), sizeof(geo::XYf)};
  geo::XY car = fr.hasPosition() ? geo::toLocal(gps.lastFix().lat, gps.lastFix().lon, fr.refLat, fr.refLon) : geo::XY{0, 0};
  if (!drawMapArea(MAP_X, MAP_Y, MAP_W, MAP_H, noLines, mp, fr.hasPosition(), car, gps.hasFix(), false)) {
    gfx->setTextDatum(MC_DATUM);
    gfx->setTextColor(C_DIM);
    gfx->drawString(fr.hasPosition() ? "Start driving -" : "Waiting for GPS", MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2 - 8, 2);
    if (fr.hasPosition()) gfx->drawString("your route draws here", MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2 + 10, 2);
    gfx->setTextDatum(TL_DATUM);
  }

  int y = MAP_Y + 2;
  gfx->setTextColor(C_DIM);
  gfx->drawString("TIME", PANEL_X, y, 2);
  gfx->setTextColor(C_TEXT);
  gfx->drawString(formatDuration(fr.elapsedMs(millis())), PANEL_X, y + 16, 4);

  y += 52;
  gfx->setTextColor(C_DIM);
  gfx->drawString("SPEED km/h", PANEL_X, y, 2);
  gfx->setTextColor(C_TEXT);
  gfx->drawNumber(lroundf(gps.lastFix().speedKmh), PANEL_X, y + 18, 6);

  y += 74;
  gfx->setTextColor(C_DIM);
  gfx->drawString("DISTANCE", PANEL_X, y, 2);
  char buf[24];
  snprintf(buf, sizeof(buf), "%.1f km", fr.distanceKm());
  gfx->setTextColor(C_TEXT);
  gfx->drawString(buf, PANEL_X, y + 16, 4);

  snprintf(buf, sizeof(buf), "top %.0f km/h", fr.topSpeedKmh());
  gfx->setTextColor(C_DIM);
  gfx->drawString(buf, PANEL_X, H - 26, 2);
  gfx->drawString("hold to end", PANEL_X, H - 10, 1);

  runningOverlays(gps, holdMs);
  endFrame();
}

void DisplayManager::drawFreeRoamSummary(const FreeRoam& fr, uint32_t durationMs, const String& savedPath) {
  beginFrame();
  // In the title, so it doesn't cover the route on the map
  header(savedPath.length() ? "DRIVE SUMMARY - tap to continue" : "DRIVE NOT SAVED - tap to continue", nullptr);
  gfx->drawRoundRect(MAP_X, MAP_Y, MAP_W, MAP_H, 6, C_BUTTON);
  static const std::vector<std::vector<geo::XYf>> noLines;
  MapPath mp = {(const uint8_t*)fr.path.data(), fr.path.size(), sizeof(geo::XYf)};
  drawMapArea(MAP_X, MAP_Y, MAP_W, MAP_H, noLines, mp, false, geo::XY{0, 0}, true, false);

  char buf[24];
  struct Row { const char* label; String value; };
  snprintf(buf, sizeof(buf), "%.2f km", fr.distanceKm());
  String dist = buf;
  snprintf(buf, sizeof(buf), "%.0f km/h", fr.topSpeedKmh());
  String top = buf;
  snprintf(buf, sizeof(buf), "%.0f km/h", fr.avgMovingKmh());
  String avg = buf;
  const Row rows[4] = {{"TIME", formatDuration(durationMs)}, {"DISTANCE", dist}, {"TOP SPEED", top}, {"AVERAGE", avg}};
  int y = MAP_Y;
  for (const Row& r : rows) {
    gfx->setTextColor(C_DIM);
    gfx->drawString(r.label, PANEL_X, y, 2);
    gfx->setTextColor(C_TEXT);
    gfx->drawString(r.value, PANEL_X, y + 14, 4);
    y += 41;
  }
  wifiButton(WIFI_BUTTON_Y, H - WIFI_BUTTON_Y - 4, false, true);
  endFrame();
}

void DisplayManager::drawSetLine(const Track& track, const GPSHandler& gps, uint32_t holdMs) {
  beginFrame();
  header(track.displayName(), &gps);

  gfx->fillRoundRect(6, HEADER_H + 8, W - 12, 120, 10, C_BUTTON);
  centered("SET START/FINISH", HEADER_H + 18, 4, C_WARN);
  centered("Drive to the start/finish line and", HEADER_H + 54, 2, C_TEXT);
  centered("TAP THE SCREEN as you cross it", HEADER_H + 74, 2, C_TEXT);
  centered("Lap 1 starts at that moment", HEADER_H + 100, 2, C_DIM);

  char buf[24];
  snprintf(buf, sizeof(buf), "%.0f km/h", gps.lastFix().speedKmh);
  centered(buf, 170, 4, C_TEXT);
  centered(gps.hasFix() ? "GPS ready" : "waiting for a GPS fix...", 200, 2, gps.hasFix() ? C_GOOD : C_WARN);
  gfx->setTextDatum(TR_DATUM);
  gfx->setTextColor(C_DIM);
  gfx->drawString("hold to end", W - 10, 224, 1);
  gfx->setTextDatum(TL_DATUM);

  runningOverlays(gps, holdMs);
  endFrame();
}

void DisplayManager::drawSummary(const Track& track, const LapTimer& timer, const String& savedPath) {
  beginFrame();
  header("SESSION SUMMARY - " + track.displayName(), nullptr);

  char buf[48];
  const Lap* best = timer.bestLap();
  int y = 34;
  gfx->setTextColor(C_DIM);
  gfx->drawString("Laps", 10, y, 2);
  gfx->drawString("Best", 80, y, 2);
  gfx->drawString("Average", 190, y, 2);
  gfx->setTextColor(C_TEXT);
  gfx->drawNumber(timer.lapCount(), 10, y + 16, 4);
  gfx->setTextColor(C_GOOD);
  gfx->drawString(best ? formatLapTime(best->timeMs) : String("-"), 80, y + 16, 4);
  gfx->setTextColor(C_TEXT);
  gfx->drawString(timer.lapCount() ? formatLapTime(timer.averageLapMs()) : String("-"), 190, y + 16, 4);

  // most recent laps, newest first
  y = 84;
  int shown = 0;
  for (int i = (int)timer.lapCount() - 1; i >= 0 && shown < 5; i--, shown++) {
    const Lap& l = timer.lap(i);
    bool isBest = (i == timer.bestLapIndex());
    snprintf(buf, sizeof(buf), "Lap %-3d %s", l.number, formatLapTime(l.timeMs).c_str());
    gfx->setTextColor(isBest ? C_GOOD : C_TEXT);
    gfx->drawString(buf, 10, y, 2);
    snprintf(buf, sizeof(buf), "%.0f km/h max", l.maxSpeedKmh);
    gfx->setTextColor(C_DIM);
    gfx->drawString(buf, 190, y, 2);
    y += 18;
  }
  if (timer.lapCount() == 0) centered("No complete laps recorded", 110, 2, C_DIM);

  gfx->setTextColor(C_DIM);
  String saved = savedPath.length() ? "Saved: " + String(Storage::sessionFSName()) + ":" + savedPath : String("Not saved");
  gfx->drawString(saved, 10, 184, 1);
  gfx->setTextDatum(MC_DATUM);
  gfx->setTextColor(C_TEXT);
  gfx->drawString("Tap or press BOOT", W / 3, WIFI_BUTTON_Y + 10, 2);
  gfx->drawString("to continue", W / 3, WIFI_BUTTON_Y + 28, 2);
  gfx->setTextDatum(TL_DATUM);
  wifiButton(WIFI_BUTTON_Y, H - WIFI_BUTTON_Y - 4, false, true);
  endFrame();
}

// ---------------------------------------------------------------------------
// Wi-Fi page

// holdHint: on the summaries, holding BOOT does the same (for boards without touch)
void DisplayManager::wifiButton(int y, int h, bool highlight, bool holdHint) {
  const int bw = W / 3, x = 2 * bw + 4;
  gfx->fillRoundRect(x, y, bw - 8, h, 6, highlight ? 0x3186 : 0x0210);
  gfx->drawRoundRect(x, y, bw - 8, h, 6, highlight ? C_TEXT : 0x041F);
  gfx->setTextDatum(MC_DATUM);
  gfx->setTextColor(C_TEXT);
  gfx->drawString("WI-FI", x + (bw - 8) / 2, holdHint ? y + h / 2 - 5 : y + h / 2, 4);
  if (holdHint) {
    gfx->setTextColor(C_DIM);
    gfx->drawString("or hold BOOT", x + (bw - 8) / 2, y + h - 6, 1);
  }
  gfx->setTextDatum(TL_DATUM);
}

void DisplayManager::drawWifi(bool wifiOn, const String& network, const String& address, int phones,
                              int uploadCode, int progress, bool restarting, bool touchOk) {
  beginFrame();
  header(wifiOn ? "WI-FI ON" : "WI-FI", nullptr);
  if (!wifiOn) {
    centered("Wi-Fi didn't start", 80, 4, C_BAD);
    centered("The serial log says why", 120, 2, C_DIM);
    if (touchOk) footerButtons("", "BACK", "");
    else centered("press BOOT: back", 212, 2, C_TEXT);
    endFrame();
    return;
  }

  centered("On your phone, join the Wi-Fi network", HEADER_H + 10, 2, C_DIM);
  centered(network, HEADER_H + 30, 4, C_TEXT);
  centered("The page opens by itself, or go to", HEADER_H + 66, 2, C_DIM);
  centered("http://" + address, HEADER_H + 86, 4, C_TEXT);

  char buf[40];
  if (restarting) {
    centered("Restarting...", 150, 4, C_WARN);
  } else if (progress >= 0) {
    snprintf(buf, sizeof(buf), "Installing firmware  %d%%", progress);
    centered(buf, 146, 2, C_WARN);
    gfx->drawRect(20, 166, W - 40, 12, C_DIM);
    gfx->fillRect(22, 168, (W - 44) * progress / 100, 8, C_WARN);
    centered("Keep the power on", 186, 2, C_DIM);
  } else {
    if (phones > 0) snprintf(buf, sizeof(buf), phones == 1 ? "1 phone connected" : "%d phones connected", phones);
    centered(phones > 0 ? buf : "No phone connected yet", 140, 2, phones > 0 ? C_GOOD : C_DIM);
    // Typed on the phone to install firmware or replace the track list
    snprintf(buf, sizeof(buf), "Upload code  %04d", uploadCode);
    centered(buf, 164, 4, C_TEXT);
  }

  if (progress < 0 && !restarting) {
    if (touchOk) footerButtons("", "WI-FI OFF", "");
    else centered("press BOOT: Wi-Fi off", 212, 2, C_TEXT);
  }
  endFrame();
}
