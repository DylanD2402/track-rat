/**
 * Track Rat - Performance mode screens (acceleration and braking runs)
 *
 * Part of DisplayManager; kept in its own file so the other screens stay as they are.
 */

#include "DisplayManager.h"
#include "Config.h"
#include "UiTheme.h"

using namespace ui;
using Font = DisplayManager::Font;
using perf::PerfResult;
using perf::PerfRun;
using perf::RunKind;
using perf::SpeedUnit;

static String seconds(float s, int decimals = 2) {
  if (s < 0) return String("–");
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
  if (m < 0) return String("–");
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f m", m);
  return String(buf);
}

// Short description of the latest run for the live screen.
static String lastRunText(const PerfResult& r, SpeedUnit u) {
  if (!r.valid) return String("Last run: invalid (GPS gap)");
  if (r.kind == RunKind::Brake) return "Last 100–0 km/h:  " + metres(r.brakeM) + ", " + seconds(r.brakeS);
  if (r.kind == RunKind::Roll) return "Last 100–200 km/h:  " + seconds(r.t100to200);
  if (u == SpeedUnit::Mph && r.t0to60mph >= 0) return "Last 0–60 mph:  " + seconds(r.t0to60mph);
  if (r.t0to100 >= 0) return "Last 0–100 km/h:  " + seconds(r.t0to100);
  if (r.t0to60 >= 0) return "Last 0–60 km/h:  " + seconds(r.t0to60);
  return String("Last run: no times");
}

void DisplayManager::drawPerfLive(const PerfRun& pr, const GPSHandler& gps, SpeedUnit u, const char* leftButton,
                                  uint32_t holdMs, bool touchOk) {
  beginFrame();
  header("Performance", &gps);

  // --- READY / GO ---
  const int by = HEADER_H + 6, bh = 32;
  const bool fix = gps.hasFix();
  uint16_t bg = C_SURFACE, fg = C_TEXT2;
  String status;
  if (!fix) {
    bg = C_RED, fg = C_BG, status = "No GPS fix";
  } else if (pr.state() == PerfRun::State::Go) {
    bg = C_ORANGE, fg = C_BG, status = "GO";
  } else if (pr.state() == PerfRun::State::Armed) {
    bg = C_GREEN, fg = C_BG, status = "Ready";
  } else {
    status = pr.speedKmh() < PerfRun::STOP_KMH ? "Hold still" : "Stop to arm";
  }
  gfx->fillRoundRect(8, by, W - 16, bh, bh / 2, bg);
  text(status, W / 2, by + bh / 2 + 1, Font::Button, fg, MC_DATUM);
  if (pr.rollout()) text("1 FT ROLLOUT", W - 22, by + bh / 2, Font::Caption, fg, MR_DATUM);

  // --- big speed, unit beside it ---
  char buf[24];
  snprintf(buf, sizeof(buf), "%.0f", perf::speedInUnit(pr.speedKmh(), u));
  int sw = textWidth(buf, Font::NumXL);
  text(buf, W / 2, by + bh + 12, Font::NumXL, C_TEXT, TC_DATUM);
  text(perf::speedLabel(u), W / 2 + sw / 2 + 8, by + bh + 12 + 56, Font::Body, C_TEXT2, BL_DATUM);

  // --- clock and distance while running, hint while waiting ---
  const int dy = 146;
  if (pr.state() == PerfRun::State::Go) {
    snprintf(buf, sizeof(buf), "%.2f s", pr.liveElapsedS());
    text(buf, 12, dy, Font::Title, C_TEXT);
    snprintf(buf, sizeof(buf), "%.0f m", pr.liveDistanceM());
    text(buf, W - 12, dy, Font::Title, C_TEXT, TR_DATUM);
  } else if (pr.state() == PerfRun::State::Armed) {
    centered("Pull away when you're ready", dy + 4, Font::Body, C_TEXT);
  } else if (fix) {
    centered("Stop for 1 s to arm", dy + 4, Font::Body, C_TEXT2);
  }

  if (!pr.results().empty()) {
    const PerfResult& r = pr.results().back();
    centered(lastRunText(r, u), 176, Font::Body, r.valid ? C_GREEN : C_YELLOW);
  } else if (fix && pr.state() != PerfRun::State::Go) {
    centered("No runs yet", 176, Font::Caption, C_TEXT3);
  }

  holdToEnd(holdMs, "Keep holding to leave");

  if (touchOk) footerButtons(leftButton, "Results", "Menu");
  else centered("BOOT: press for results, hold for the menu", 212, Font::Body, C_TEXT3);
  endFrame();
}

// A big number with its unit after it in a smaller font, centred.
void DisplayManager::perfBigValue(float value, int decimals, const char* unit, int y, uint16_t color) {
  char num[16];
  if (value < 0) snprintf(num, sizeof(num), "-");
  else snprintf(num, sizeof(num), "%.*f", decimals, value);
  const int gap = 8;
  int nw = textWidth(num, Font::NumL);
  int total = nw + (value < 0 ? 0 : gap + textWidth(unit, Font::Title));
  int x = (W - total) / 2;
  text(num, x, y, Font::NumL, color);
  if (value >= 0) text(unit, x + nw + gap, y + 37, Font::Title, C_TEXT2, BL_DATUM);
}

// One line of a result card: label, value, and a smaller extra on the right (trap speed).
void DisplayManager::perfRow(int y, const char* name, const String& value, const String& extra, uint16_t valueColor) {
  const int mid = y + 13;
  text(name, 12, mid, Font::Body, C_TEXT2, ML_DATUM);
  text(value, 136, mid, Font::Title, value == "–" ? C_TEXT3 : valueColor, ML_DATUM);
  if (extra.length()) text(extra, W - 12, mid, Font::Body, C_TEXT2, MR_DATUM);
  gfx->drawFastHLine(12, y + 27, W - 24, C_SURFACE);
}

// index >= 0: that run's card; index < 0: the session's best.
void DisplayManager::drawPerfCard(const PerfRun& pr, int index, SpeedUnit u, bool touchOk) {
  beginFrame();
  const bool isBest = index < 0;
  const PerfResult best = pr.best();
  const std::vector<PerfResult>& all = pr.results();
  char buf[48];

  if (isBest) {
    int valid = 0;
    for (const PerfResult& r : all) valid += r.valid;
    snprintf(buf, sizeof(buf), "Session best   ·   %d run%s", valid, valid == 1 ? "" : "s");
  } else if (index < (int)all.size()) {
    const char* kind = all[index].kind == RunKind::Launch ? "launch" : all[index].kind == RunKind::Roll ? "rolling" : "braking";
    snprintf(buf, sizeof(buf), "Run %d of %d   ·   %s", index + 1, (int)all.size(), kind);
  } else {
    snprintf(buf, sizeof(buf), "Results");
  }
  header(buf, nullptr);

  if (!isBest && index >= (int)all.size()) {
    centered("No runs yet", 96, Font::Title, C_TEXT3);
  } else {
    const PerfResult& r = isBest ? best : all[index];
    const uint16_t vc = isBest ? C_GREEN : C_TEXT;
    const int y0 = HEADER_H + 2, rh = 28;
    const bool mph = u == SpeedUnit::Mph;

    if (!isBest && r.kind == RunKind::Brake) {
      label("BRAKING 100–0 KM/H", 12, y0 + 8);
      perfBigValue(r.brakeM, 1, "m", y0 + 32, r.valid ? vc : C_TEXT3);
      if (mph && r.brakeM >= 0) {
        snprintf(buf, sizeof(buf), "%.0f ft", r.brakeM / perf::METRES_PER_FOOT);
        centered(buf, y0 + 80, Font::Title, C_TEXT2);
      }
      perfRow(y0 + 112, "Time", seconds(r.brakeS), "", vc);
    } else if (!isBest && r.kind == RunKind::Roll) {
      label("ROLLING 100–200 KM/H", 12, y0 + 8);
      perfBigValue(r.t100to200, 2, "s", y0 + 32, r.valid ? vc : C_TEXT3);
    } else {
      int y = y0;
      if (mph) {
        perfRow(y, "0–60 mph", seconds(r.t0to60mph), "", vc), y += rh;
        perfRow(y, "0–100 km/h", seconds(r.t0to100), "", vc), y += rh;
      } else {
        perfRow(y, "0–100 km/h", seconds(r.t0to100), "", vc), y += rh;
        perfRow(y, "0–60 km/h", seconds(r.t0to60), "", vc), y += rh;
      }
      perfRow(y, "100–200 km/h", seconds(r.t100to200), "", vc), y += rh;
      perfRow(y, "⅛ mile", seconds(r.et8), speedText(r.trap8Kmh, u), vc), y += rh;
      perfRow(y, "¼ mile", seconds(r.et4), speedText(r.trap4Kmh, u), vc), y += rh;
      if (isBest) {
        String d = r.brakeM >= 0 ? seconds(r.brakeS) : String("");
        perfRow(y, "100–0 km/h", metres(r.brakeM), d, vc);
      }
    }

    if (!isBest) {
      const int by = 172;
      if (!r.valid) {
        gfx->fillRoundRect(8, by, W - 16, 24, 12, C_RED);
        text("Invalid: GPS signal gap", W / 2, by + 12, Font::Body, C_BG, MC_DATUM);
      } else if (r.notLevel) {
        snprintf(buf, sizeof(buf), "Not level: %s %.1f m (%.1f%%)", r.climbM >= 0 ? "uphill" : "downhill",
                 fabsf(r.climbM), fabsf(r.gradePct));
        gfx->fillRoundRect(8, by, W - 16, 24, 12, C_YELLOW);
        text(buf, W / 2, by + 12, Font::Body, C_BG, MC_DATUM);
      } else if (r.kind == RunKind::Launch && r.rollout) {
        text("Timed from 1 ft of rollout", 12, by + 12, Font::Caption, C_TEXT2, ML_DATUM);
      }
    }
  }

  if (touchOk) {
    if (isBest) footerButtons("Runs", "", "Live");
    else footerButtons("Prev", "Best", "Live");
  } else {
    centered("BOOT: press for the next screen, hold for the menu", 212, Font::Body, C_TEXT3);
  }
  endFrame();
}
