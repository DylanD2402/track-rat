/**
 * Track Rat - Performance mode screens (acceleration and braking runs)
 *
 * Part of DisplayManager; kept in its own file so the other screens stay as they are.
 */

#include "DisplayManager.h"
#include "Config.h"

static const int W = 320;
static const int H = 240;
static const int HEADER_H = 26;

static const uint16_t C_BG = TFT_BLACK;
static const uint16_t C_TEXT = TFT_WHITE;
static const uint16_t C_DIM = TFT_DARKGREY;
static const uint16_t C_GOOD = TFT_GREEN;
static const uint16_t C_BAD = TFT_RED;
static const uint16_t C_WARN = TFT_YELLOW;
static const uint16_t C_BUTTON = 0x2124;
static const uint16_t C_HEADER = 0x10A2;
static const uint16_t C_GO = 0xFD20;  // orange

using perf::PerfResult;
using perf::PerfRun;
using perf::RunKind;
using perf::SpeedUnit;

static String seconds(float s, int decimals = 2) {
  if (s < 0) return String("--");
  char buf[16];
  snprintf(buf, sizeof(buf), "%.*f s", decimals, s);
  return String(buf);
}

static String speedText(float kmh, SpeedUnit u) {
  if (kmh < 0) return String("");
  char buf[20];
  snprintf(buf, sizeof(buf), "%.0f %s", perf::speedInUnit(kmh, u), perf::speedLabel(u));
  return String(buf);
}

static String metres(float m) {
  if (m < 0) return String("--");
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f m", m);
  return String(buf);
}

// Short description of the latest run for the live screen.
static String lastRunText(const PerfResult& r, SpeedUnit u) {
  if (!r.valid) return String("LAST RUN: invalid (GPS gap)");
  if (r.kind == RunKind::Brake) return "LAST 100-0 km/h: " + metres(r.brakeM) + ", " + seconds(r.brakeS);
  if (r.kind == RunKind::Roll) return "LAST 100-200 km/h: " + seconds(r.t100to200);
  if (u == SpeedUnit::Mph && r.t0to60mph >= 0) return "LAST 0-60 mph: " + seconds(r.t0to60mph);
  if (r.t0to100 >= 0) return "LAST 0-100 km/h: " + seconds(r.t0to100);
  if (r.t0to60 >= 0) return "LAST 0-60 km/h: " + seconds(r.t0to60);
  return String("LAST RUN: no times");
}

void DisplayManager::drawPerfLive(const PerfRun& pr, const GPSHandler& gps, SpeedUnit u, const char* leftButton,
                                  uint32_t holdMs, bool touchOk) {
  beginFrame();
  header("PERFORMANCE", &gps);

  // --- READY / GO ---
  const int by = HEADER_H + 4, bh = 36;
  const bool fix = gps.hasFix();
  uint16_t bg = C_BUTTON, fg = C_DIM;
  String text;
  if (!fix) {
    bg = C_BAD, fg = C_TEXT, text = "NO GPS FIX";
  } else if (pr.state() == PerfRun::State::Go) {
    bg = C_GO, fg = C_BG, text = "GO";
  } else if (pr.state() == PerfRun::State::Armed) {
    bg = C_GOOD, fg = C_BG, text = "READY";
  } else {
    text = pr.speedKmh() < PerfRun::STOP_KMH ? "HOLD STILL" : "STOP TO ARM";
  }
  gfx->fillRoundRect(6, by, W - 12, bh, 8, bg);
  gfx->setTextDatum(MC_DATUM);
  gfx->setTextColor(fg);
  gfx->drawString(text, W / 2, by + bh / 2 + 1, 4);
  if (pr.rollout()) {
    gfx->setTextDatum(MR_DATUM);
    gfx->drawString("ROLLOUT", W - 14, by + bh / 2 + 1, 2);
  }
  gfx->setTextDatum(TL_DATUM);

  // --- big speed ---
  char buf[24];
  snprintf(buf, sizeof(buf), "%.0f", perf::speedInUnit(pr.speedKmh(), u));
  centered(buf, by + bh + 4, 8, C_TEXT);
  gfx->setTextDatum(BR_DATUM);
  gfx->setTextColor(C_DIM);
  gfx->drawString(perf::speedLabel(u), W - 8, by + bh + 82, 2);
  gfx->setTextDatum(TL_DATUM);

  // --- clock and distance while running, hint while waiting ---
  const int dy = 158;
  if (pr.state() == PerfRun::State::Go) {
    snprintf(buf, sizeof(buf), "%.2f s", pr.liveElapsedS());
    gfx->setTextColor(C_TEXT);
    gfx->drawString(buf, 10, dy, 4);
    snprintf(buf, sizeof(buf), "%.0f m", pr.liveDistanceM());
    gfx->setTextDatum(TR_DATUM);
    gfx->drawString(buf, W - 10, dy, 4);
    gfx->setTextDatum(TL_DATUM);
  } else if (pr.state() == PerfRun::State::Armed) {
    centered("Pull away when you're ready", dy + 4, 2, C_TEXT);
  } else if (fix) {
    centered("Stop for 1 s to arm", dy + 4, 2, C_DIM);
  }

  if (!pr.results().empty()) {
    gfx->setTextColor(pr.results().back().valid ? C_GOOD : C_WARN);
    gfx->drawString(lastRunText(pr.results().back(), u), 10, 184, 2);
  } else if (fix && pr.state() != PerfRun::State::Go) {
    centered("No runs yet", 184, 2, C_DIM);
  }

  if (holdMs > 150) {
    int w = (int)min((uint32_t)(W - 40), (uint32_t)((W - 40) * holdMs / END_SESSION_HOLD_MS));
    gfx->fillRect(10, 90, W - 20, 50, C_HEADER);
    centered("Keep holding to leave", 96, 2, C_TEXT);
    gfx->fillRect(20, 118, w, 12, C_WARN);
  }

  if (touchOk) footerButtons(leftButton, "RESULTS", "MENU");
  else centered("BOOT: tap = results, hold = menu", 212, 2, C_DIM);
  endFrame();
}

// A big number (font 6 has digits only) with its unit after it in a smaller font, centred.
static void bigValue(TFT_eSPI* gfx, float value, int decimals, const char* unit, int y, uint16_t color) {
  char num[16];
  if (value < 0) snprintf(num, sizeof(num), "--");
  else snprintf(num, sizeof(num), "%.*f", decimals, value);
  const int gap = 8;
  int total = gfx->textWidth(num, 6) + (value < 0 ? 0 : gap + gfx->textWidth(unit, 4));
  int x = (W - total) / 2;
  gfx->setTextDatum(TL_DATUM);
  gfx->setTextColor(color);
  gfx->drawString(num, x, y, 6);
  if (value >= 0) gfx->drawString(unit, x + gfx->textWidth(num, 6) + gap, y + 22, 4);
}

// One line of a result card: label, value, and a smaller extra on the right (trap speed).
static void resultRow(TFT_eSPI* gfx, int y, const char* label, const String& value, const String& extra,
                      uint16_t valueColor) {
  gfx->setTextDatum(TL_DATUM);
  gfx->setTextColor(C_DIM);
  gfx->drawString(label, 10, y + 6, 2);
  gfx->setTextColor(value == "--" ? C_DIM : valueColor);
  gfx->drawString(value, 128, y + 1, 4);
  if (extra.length()) {
    gfx->setTextColor(C_DIM);
    gfx->drawString(extra, 228, y + 6, 2);
  }
}

// index >= 0: that run's card; index < 0: the session's best.
void DisplayManager::drawPerfCard(const PerfRun& pr, int index, SpeedUnit u, bool touchOk) {
  beginFrame();
  const bool isBest = index < 0;
  const PerfResult best = pr.best();
  const std::vector<PerfResult>& all = pr.results();
  char buf[40];

  if (isBest) {
    int valid = 0;
    for (const PerfResult& r : all) valid += r.valid;
    snprintf(buf, sizeof(buf), "SESSION BEST - %d run%s", valid, valid == 1 ? "" : "s");
  } else if (index < (int)all.size()) {
    const char* kind = all[index].kind == RunKind::Launch ? "LAUNCH" : all[index].kind == RunKind::Roll ? "ROLLING" : "BRAKING";
    snprintf(buf, sizeof(buf), "RUN %d OF %d - %s", index + 1, (int)all.size(), kind);
  } else {
    snprintf(buf, sizeof(buf), "RESULTS");
  }
  header(buf, nullptr);

  if (!isBest && index >= (int)all.size()) {
    centered("No runs yet", 100, 4, C_DIM);
  } else {
    const PerfResult& r = isBest ? best : all[index];
    const uint16_t vc = isBest ? C_GOOD : C_TEXT;
    const int y0 = HEADER_H + 4, rh = 28;
    const bool mph = u == SpeedUnit::Mph;

    if (!isBest && r.kind == RunKind::Brake) {
      gfx->setTextColor(C_DIM);
      gfx->drawString("BRAKING 100-0 km/h", 10, y0 + 4, 2);
      bigValue(gfx, r.brakeM, 1, "m", y0 + 30, r.valid ? vc : C_DIM);
      if (mph && r.brakeM >= 0) {
        snprintf(buf, sizeof(buf), "%.0f ft", r.brakeM / perf::METRES_PER_FOOT);
        centered(buf, y0 + 84, 4, C_DIM);
      }
      resultRow(gfx, y0 + 116, "TIME", seconds(r.brakeS), "", vc);
    } else if (!isBest && r.kind == RunKind::Roll) {
      gfx->setTextColor(C_DIM);
      gfx->drawString("ROLLING 100-200 km/h", 10, y0 + 4, 2);
      bigValue(gfx, r.t100to200, 2, "s", y0 + 30, r.valid ? vc : C_DIM);
    } else {
      int y = y0;
      if (mph) {
        resultRow(gfx, y, "0-60 mph", seconds(r.t0to60mph), "", vc), y += rh;
        resultRow(gfx, y, "0-100 km/h", seconds(r.t0to100), "", vc), y += rh;
      } else {
        resultRow(gfx, y, "0-100 km/h", seconds(r.t0to100), "", vc), y += rh;
        resultRow(gfx, y, "0-60 km/h", seconds(r.t0to60), "", vc), y += rh;
      }
      resultRow(gfx, y, "100-200 km/h", seconds(r.t100to200), "", vc), y += rh;
      resultRow(gfx, y, "1/8 mile", seconds(r.et8), speedText(r.trap8Kmh, u), vc), y += rh;
      resultRow(gfx, y, "1/4 mile", seconds(r.et4), speedText(r.trap4Kmh, u), vc), y += rh;
      if (isBest) {
        String d = r.brakeM >= 0 ? seconds(r.brakeS) : String("");
        resultRow(gfx, y, "100-0 km/h", metres(r.brakeM), d, vc);
      }
    }

    if (!isBest) {
      const int by = 178;
      if (!r.valid) {
        gfx->fillRoundRect(6, by, W - 12, 22, 5, C_BAD);
        gfx->setTextColor(C_TEXT);
        gfx->setTextDatum(MC_DATUM);
        gfx->drawString("INVALID: GPS signal gap", W / 2, by + 11, 2);
        gfx->setTextDatum(TL_DATUM);
      } else if (r.notLevel) {
        snprintf(buf, sizeof(buf), "NOT LEVEL: %s %.1f m (%.1f%%)", r.climbM >= 0 ? "uphill" : "downhill",
                 fabsf(r.climbM), fabsf(r.gradePct));
        gfx->fillRoundRect(6, by, W - 12, 22, 5, C_WARN);
        gfx->setTextColor(C_BG);
        gfx->setTextDatum(MC_DATUM);
        gfx->drawString(buf, W / 2, by + 11, 2);
        gfx->setTextDatum(TL_DATUM);
      } else if (r.kind == RunKind::Launch && r.rollout) {
        gfx->setTextColor(C_DIM);
        gfx->drawString("timed from 1 ft of rollout", 10, by + 4, 2);
      }
    }
  }

  if (touchOk) {
    if (isBest) footerButtons("RUNS", "", "LIVE");
    else footerButtons("PREV", "BEST", "LIVE");
  } else {
    centered("BOOT: tap = next screen, hold = menu", 212, 2, C_DIM);
  }
  endFrame();
}
