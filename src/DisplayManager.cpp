/**
 * Track Rat - Display implementation
 */

#include "DisplayManager.h"
#include "Config.h"
#include "SessionLog.h"
#include "Storage.h"

static const int W = 320;
static const int H = 240;
static const int HEADER_H = 26;
static const int FOOTER_Y = 202;

static const uint16_t C_BG = TFT_BLACK;
static const uint16_t C_TEXT = TFT_WHITE;
static const uint16_t C_DIM = TFT_DARKGREY;
static const uint16_t C_HEADER = 0x10A2;  // dark slate
static const uint16_t C_ACCENT = TFT_CYAN;
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

void DisplayManager::header(const String& title, const GPSHandler* gps) {
  gfx->fillRect(0, 0, W, HEADER_H, C_HEADER);
  gfx->setTextColor(C_TEXT);
  String t = title;
  int maxW = gps ? W - 84 : W - 12;  // leave room for the signal bars
  while (t.length() > 3 && gfx->textWidth(t, 2) > maxW) t.remove(t.length() - 1);
  gfx->drawString(t, 6, 5, 2);
  if (gps) {
    gpsBadge(*gps);
    if (gps->isSimulated()) {  // make simulated timing impossible to miss
      gfx->fillRoundRect(W - 78, 4, 34, HEADER_H - 8, 4, C_WARN);
      gfx->setTextDatum(MC_DATUM);
      gfx->setTextColor(C_BG);
      gfx->drawString("SIM", W - 61, HEADER_H / 2, 2);
      gfx->setTextDatum(TL_DATUM);
    }
  }
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
  beginFrame();
  centered("TRACK RAT", 60, 4, C_ACCENT);
  centered("GPS lap timer", 95, 2, C_TEXT);
  centered(status, 160, 2, C_DIM);
  endFrame();
}

void DisplayManager::drawGpsWait(const GPSHandler& gps, int trackCount, const char* trackSource,
                                 bool touchOk) {
  beginFrame();
  header("TRACK RAT - waiting for GPS", &gps);

  char buf[64];
  int y = 36;
  gfx->setTextColor(C_DIM);
  gfx->drawString("Data", 10, y, 2);
  if (gps.receivingData()) {
    if (gps.isSimulated()) snprintf(buf, sizeof(buf), "SIMULATED - no GPS detected");
    else snprintf(buf, sizeof(buf), "NMEA OK @ %lu, GPIO %d", (unsigned long)gps.baud(), gps.rxPinUsed());
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
  gfx->drawString(gps.hasFix() ? "LOCKED" : "searching...", 90, y, 2);

  y += 24;
  gfx->setTextColor(C_DIM);
  gfx->drawString("Satellites", 10, y, 2);
  snprintf(buf, sizeof(buf), "%d   HDOP %.1f   %.1f Hz", gps.satellites(), gps.hdop(), gps.fixRateHz());
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

  centered(touchOk ? "Tap screen (or BOOT) to pick a track now" : "Press BOOT to pick a track now", 210, 2, C_DIM);
  endFrame();
}

void DisplayManager::drawTrackSelect(const Track& track, int index, int count, double distanceM, bool touchOk) {
  beginFrame();
  char buf[32];
  snprintf(buf, sizeof(buf), "SELECT TRACK   %d / %d", index + 1, count);
  header(buf, nullptr);

  centered(track.name, 44, 4, C_ACCENT);
  centered(track.location + (track.country.length() ? ", " + track.country : String("")), 82, 2, C_TEXT);
  if (track.length > 0) {
    snprintf(buf, sizeof(buf), "%.2f km", track.length / 1000.0);
    centered(buf, 108, 4, C_TEXT);
  }
  if (distanceM >= 0) centered(distanceText(distanceM) + " from you", 146, 2, C_DIM);
  if (!track.splits.empty()) {
    snprintf(buf, sizeof(buf), "%d split%s", (int)track.splits.size(), track.splits.size() > 1 ? "s" : "");
    centered(buf, 168, 2, C_DIM);
  }

  if (touchOk) footerButtons("<", "SELECT", ">");
  else centered("BOOT: tap = next, hold = select", 212, 2, C_DIM);
  endFrame();
}

void DisplayManager::drawTrackConfirm(const Track& track, double distanceM, bool gpsFix, bool touchOk) {
  beginFrame();
  header("READY", nullptr);

  centered(track.name, 40, 4, C_ACCENT);
  if (gpsFix && distanceM >= 0) {
    centered("Start/finish is " + distanceText(distanceM) + " away", 84, 2,
             distanceM < 2000 ? C_GOOD : C_WARN);
  } else {
    centered("No GPS fix yet", 84, 2, C_WARN);
  }
  centered("Timing starts when you cross", 120, 2, C_TEXT);
  centered("the start/finish line", 140, 2, C_TEXT);
  centered(String("Laps are saved to ") + Storage::sessionFSName(), 168, 2, C_DIM);

  if (touchOk) footerButtons("BACK", "START", "");
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

  runningOverlays(gps, holdMs);
  endFrame();
}

void DisplayManager::runningOverlays(const GPSHandler& gps, uint32_t holdMs) {
  if (!gps.hasFix()) {
    gfx->fillRect(0, H - 30, W, 30, C_BAD);
    centered("NO GPS FIX", H - 26, 4, C_TEXT);
  }

  if (holdMs > 150) {
    int w = (int)min((uint32_t)(W - 40), (uint32_t)((W - 40) * holdMs / END_SESSION_HOLD_MS));
    gfx->fillRect(10, 90, W - 20, 50, C_HEADER);
    centered("Keep holding to end session", 96, 2, C_TEXT);
    gfx->fillRect(20, 118, w, 12, C_WARN);
  }
}

String DisplayManager::runningTitle(const Track& track, const LapTimer& timer) {
  if (!timer.lapRunning()) return track.name;
  char buf[16];
  snprintf(buf, sizeof(buf), "LAP %d   ", timer.currentLapNumber());
  return buf + track.name;  // lap number first so a long name gets truncated, not the lap
}

// ---------------------------------------------------------------------------
// Map view: track layout on the left, timing on the right.

static const int MAP_X = 2, MAP_Y = HEADER_H + 2, MAP_W = 212, MAP_H = H - HEADER_H - 4;
static const int PANEL_X = MAP_X + MAP_W + 6;
static const uint16_t C_TRACK = 0xBDF7;  // light grey

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

  // Bounding box in metres
  float minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
  auto grow = [&](float x, float y) {
    minX = min(minX, x); maxX = max(maxX, x);
    minY = min(minY, y); maxY = max(maxY, y);
  };
  for (const auto& line : track.map)
    for (const auto& p : line) grow(p.x, p.y);
  if (path)
    for (const auto& p : *path) grow(p.x, p.y);

  geo::XY car = geo::toLocal(gps.lastFix().lat, gps.lastFix().lon, track.startFinish.lat, track.startFinish.lon);
  bool haveShape = maxX > minX || maxY > minY;

  if (!haveShape) {
    gfx->setTextDatum(MC_DATUM);
    gfx->setTextColor(C_DIM);
    gfx->drawString("No map for this track", MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2 - 10, 2);
    gfx->drawString("It draws itself once", MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2 + 10, 2);
    gfx->drawString("your first lap starts", MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2 + 28, 2);
    gfx->setTextDatum(TL_DATUM);
  } else {
    const float pad = 10;
    float bw = max(maxX - minX, 1.0f), bh = max(maxY - minY, 1.0f);
    float scale = min((MAP_W - 2 * pad) / bw, (MAP_H - 2 * pad) / bh);
    float cx = MAP_X + MAP_W / 2.0f, cy = MAP_Y + MAP_H / 2.0f;
    float midX = (minX + maxX) / 2, midY = (minY + maxY) / 2;
    auto sx = [&](float x) { return (int)lroundf(cx + (x - midX) * scale); };
    auto sy = [&](float y) { return (int)lroundf(cy - (y - midY) * scale); };  // north up

    auto thickLine = [&](int x0, int y0, int x1, int y1, uint16_t c) {
      gfx->drawLine(x0, y0, x1, y1, c);
      gfx->drawLine(x0 + 1, y0, x1 + 1, y1, c);
      gfx->drawLine(x0, y0 + 1, x1, y1 + 1, c);
    };

    for (const auto& line : track.map)
      for (size_t i = 1; i < line.size(); i++)
        thickLine(sx(line[i - 1].x), sy(line[i - 1].y), sx(line[i].x), sy(line[i].y), C_TRACK);

    if (path) {
      size_t step = max((size_t)1, path->size() / 800);
      size_t prev = 0;
      for (size_t i = step; i < path->size(); i += step) {
        thickLine(sx((*path)[prev].x), sy((*path)[prev].y), sx((*path)[i].x), sy((*path)[i].y), C_TRACK);
        prev = i;
      }
    }

    // Start/finish marker
    gfx->fillRect(sx(0) - 4, sy(0) - 4, 9, 9, C_TEXT);
    gfx->drawRect(sx(0) - 4, sy(0) - 4, 9, 9, C_BG);

    // Car, kept inside the map frame if it's off the drawn area
    if (gps.lastFix().rxMillis != 0) {
      int px = constrain(sx(car.x), MAP_X + 7, MAP_X + MAP_W - 8);
      int py = constrain(sy(car.y), MAP_Y + 7, MAP_Y + MAP_H - 8);
      gfx->fillCircle(px, py, 7, C_TEXT);
      gfx->fillCircle(px, py, 5, gps.hasFix() ? C_ACCENT : C_WARN);
    }
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

  runningOverlays(gps, holdMs);
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

void DisplayManager::drawSummary(const Track& track, const LapTimer& timer, const String& savedPath) {
  beginFrame();
  header("SESSION SUMMARY - " + track.name, nullptr);

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
  gfx->drawString(saved, 10, 186, 1);
  centered("Tap or press BOOT to continue", 212, 2, C_TEXT);
  endFrame();
}
