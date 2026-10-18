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
  gfx = useSprite ? (TFT_eSPI*)&spr : &tft;
  DEBUG_PRINTF("Display: %dx%d, %s\n", tft.width(), tft.height(),
               useSprite ? "sprite buffer in PSRAM" : "NO sprite (PSRAM missing?) - direct draw");
  return true;
}

void DisplayManager::beginFrame() {
  gfx->fillScreen(C_BG);
  gfx->setTextDatum(TL_DATUM);
}

void DisplayManager::endFrame() {
  if (useSprite) spr.pushSprite(0, 0);
}

void DisplayManager::centered(const String& s, int y, int font, uint16_t color) {
  if (font == 4 && gfx->textWidth(s, 4) > W - 8) font = 2;  // long names: smaller font
  gfx->setTextDatum(TC_DATUM);
  gfx->setTextColor(color);
  gfx->drawString(s, W / 2, y, font);
  gfx->setTextDatum(TL_DATUM);
}

void DisplayManager::gpsBadge(const GPSHandler& gps) {
  uint16_t dot = gps.hasFix() ? C_GOOD : (gps.receivingData() ? C_WARN : C_BAD);
  char buf[24];
  snprintf(buf, sizeof(buf), "%d sat %.0fHz", gps.satellites(), gps.fixRateHz());
  gfx->setTextDatum(TR_DATUM);
  gfx->setTextColor(C_TEXT);
  gfx->drawString(buf, W - 20, 5, 2);
  gfx->fillCircle(W - 10, 12, 5, dot);
  gfx->setTextDatum(TL_DATUM);
}

void DisplayManager::header(const String& title, const GPSHandler* gps) {
  gfx->fillRect(0, 0, W, HEADER_H, C_HEADER);
  gfx->setTextColor(C_TEXT);
  String t = title;
  int maxW = gps ? W - 120 : W - 12;  // leave room for the GPS badge
  while (t.length() > 3 && gfx->textWidth(t, 2) > maxW) t.remove(t.length() - 1);
  gfx->drawString(t, 6, 5, 2);
  if (gps) gpsBadge(*gps);
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
#if SIMULATE_GPS
  centered("SIMULATOR BUILD", 120, 2, C_WARN);
#endif
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
#if SIMULATE_GPS
    snprintf(buf, sizeof(buf), "simulated");
#else
    snprintf(buf, sizeof(buf), "NMEA OK @ %lu baud", (unsigned long)gps.baud());
#endif
    gfx->setTextColor(C_GOOD);
  } else {
    snprintf(buf, sizeof(buf), "NO DATA - check GPS wiring");
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
  String title = track.name;
  if (timer.lapRunning()) {
    snprintf(buf, sizeof(buf), "LAP %d   ", timer.currentLapNumber());
    title = buf + title;  // lap number first so a long name gets truncated, not the lap
  }
  header(title, &gps);

  // --- big time area (y 28..124) ---
  if (ov.showLap) {
    uint16_t bg = ov.lapIsBest ? 0x0320 : 0x0010;
    gfx->fillRect(0, HEADER_H + 2, W, 96, bg);
    snprintf(buf, sizeof(buf), ov.lapIsBest ? "LAP %d  -  BEST LAP" : "LAP %d", ov.lapNumber);
    gfx->setTextDatum(TC_DATUM);
    gfx->setTextColor(C_TEXT);
    gfx->drawString(buf, W / 2, HEADER_H + 6, 2);
    gfx->drawString(formatLapTime(ov.lapMs, 3), W / 2, HEADER_H + 28, 7);
    gfx->setTextDatum(TL_DATUM);
  } else if (timer.lapRunning()) {
    String t = formatLapTime(timer.currentLapMs(millis()), 1);
    int font = gfx->textWidth(t, 8) <= W - 10 ? 8 : 7;
    centered(t, font == 8 ? HEADER_H + 10 : HEADER_H + 24, font, C_TEXT);
  } else {
    centered("OUT LAP", HEADER_H + 22, 4, C_WARN);
    centered("Cross start/finish to begin timing", HEADER_H + 62, 2, C_DIM);
  }

  // --- last / best (y 128..170) ---
  const int rowY = 128;
  gfx->setTextColor(C_DIM);
  gfx->drawString("LAST", 10, rowY, 2);
  gfx->drawString("BEST", W / 2 + 10, rowY, 2);
  const Lap* last = timer.lastLap();
  const Lap* best = timer.bestLap();
  gfx->setTextColor(C_TEXT);
  gfx->drawString(last ? formatLapTime(last->timeMs) : String("-:--.---"), 10, rowY + 16, 4);
  gfx->setTextColor(best ? C_GOOD : C_TEXT);
  gfx->drawString(best ? formatLapTime(best->timeMs) : String("-:--.---"), W / 2 + 10, rowY + 16, 4);

  // --- delta box (y 172..200) ---
  const int dY = 174;
  if (ov.splitIndex >= 0) {
    snprintf(buf, sizeof(buf), "S%d  %s", ov.splitIndex + 1, formatLapTime(ov.splitMs).c_str());
    gfx->setTextColor(C_TEXT);
    gfx->drawString(buf, 10, dY, 4);
    if (ov.deltaMs != NO_DELTA) {
      gfx->setTextDatum(TR_DATUM);
      gfx->setTextColor(ov.deltaMs <= 0 ? C_GOOD : C_BAD);
      gfx->drawString(formatDelta(ov.deltaMs), W - 10, dY, 4);
      gfx->setTextDatum(TL_DATUM);
    }
  } else if (last && best && last != best) {
    gfx->setTextColor(C_DIM);
    gfx->drawString("LAST vs BEST", 10, dY + 6, 2);
    int32_t d = (int32_t)last->timeMs - (int32_t)best->timeMs;
    gfx->setTextDatum(TR_DATUM);
    gfx->setTextColor(d <= 0 ? C_GOOD : C_BAD);
    gfx->drawString(formatDelta(d), W - 10, dY, 4);
    gfx->setTextDatum(TL_DATUM);
  }

  // --- bottom line ---
  snprintf(buf, sizeof(buf), "%.0f km/h", gps.lastFix().speedKmh);
  gfx->setTextColor(C_TEXT);
  gfx->drawString(buf, 10, 214, 4);
  gfx->setTextDatum(TR_DATUM);
  gfx->setTextColor(C_DIM);
  gfx->drawString("hold to end", W - 10, 220, 2);
  gfx->setTextDatum(TL_DATUM);

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
  endFrame();
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
