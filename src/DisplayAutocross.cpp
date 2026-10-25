/**
 * Track Rat - the Autocross hub and New course screens (9.2)
 *
 * Part of DisplayManager; kept in its own file so the other screens stay as they are.
 * Which course and which runs are chosen in main.cpp (Courses.h, CourseRuns.h); this only lays them out.
 */

#include "DisplayManager.h"
#include "Config.h"
#include "Settings.h"
#include "UiTheme.h"

using namespace ui;
using Font = DisplayManager::Font;

// A run time as seen on the course: "52.31", or "1:02.31" from a minute (hundredths, cut not rounded, like the clock)
static String runTime(uint32_t ms) {
  char buf[16];
  const unsigned long cs = (ms / 10) % 100, s = (ms / 1000) % 60, m = ms / 60000;
  if (m) snprintf(buf, sizeof(buf), "%lu:%02lu.%02lu", m, s, cs);
  else snprintf(buf, sizeof(buf), "%lu.%02lu", s, cs);
  return String(buf);
}

void DisplayManager::drawAutocross(const AutocrossView& v, const GPSHandler& gps, int highlight, bool touchOk) {
  beginFrame();
  header("Autocross", &gps);

  // --- the course card ---
  const int cx = 6, cy = HEADER_H + 5, cw = W - 12, ch = 116;
  gfx->fillRoundRect(cx, cy, cw, ch, CORNER, C_SURFACE);
  if (!v.haveCourse) {
    text("No course here today", W / 2, cy + 34, Font::Title, C_TEXT, TC_DATUM);
    text(v.gpsFix ? "At the start of the course," : "Needs a GPS fix to find or make a course",
         W / 2, cy + 66, Font::Caption, C_TEXT2, TC_DATUM);
    if (v.gpsFix) text("press New course", W / 2, cy + 82, Font::Caption, C_TEXT2, TC_DATUM);
  } else {
    // Name on the left; "Run 4 next" (today's course) or its date on the right
    String right;
    uint16_t rightColour = C_TEXT3;
    if (v.today && v.nextRun > 0) {
      right = "Run " + String(v.nextRun) + " next";
      rightColour = C_GREEN;
    } else if (v.dateText.length()) {
      right = v.dateText;
    }
    const int rw = right.length() ? textWidth(right, Font::Body) : 0;
    text(fit(v.name, Font::Title, cw - 24 - rw - 8), cx + 12, cy + 6, Font::Title, C_TEXT);
    if (rw) text(right, cx + cw - 12, cy + 10, Font::Body, rightColour, TR_DATUM);

    // Best and last
    label("BEST", cx + 12, cy + 32);
    label("LAST", cx + 164, cy + 32);
    if (v.best >= 0 && v.best < (int)v.runs.size()) {
      const int w = text(runTime(v.runs[v.best].timeMs), cx + 12, cy + 43, Font::Title, C_GREEN);
      text("run " + String(v.best + 1), cx + 12 + w + 6, cy + 49, Font::Caption, C_TEXT2);
    } else {
      text(v.loading ? "…" : "–", cx + 12, cy + 43, Font::Title, C_TEXT3);
    }
    if (v.last >= 0 && v.last < (int)v.runs.size()) {
      const int w = text(runTime(v.runs[v.last].timeMs), cx + 164, cy + 43, Font::Title, C_TEXT);
      text("run " + String(v.last + 1), cx + 164 + w + 6, cy + 49, Font::Caption, C_TEXT2);
    } else {
      text(v.loading ? "…" : "–", cx + 164, cy + 43, Font::Title, C_TEXT3);
    }

    // The runs, four to a row, the latest twelve; the best in green with a dot
    gfx->drawFastHLine(cx + 12, cy + 70, cw - 24, C_LINE);
    if (v.runs.empty()) {
      text(v.loading ? "Reading the day's runs…" : "No runs yet", cx + 12, cy + 82, Font::Caption, C_TEXT3);
    } else {
      const int n = (int)v.runs.size(), first = n > 12 ? n - 12 : 0;
      for (int i = first; i < n; i++) {
        const int k = i - first, x = cx + 12 + (k % 4) * 73, y = cy + 75 + (k / 4) * 13;
        const bool best = i == v.best;
        const uint16_t c = best ? C_GREEN : laptags::clean(v.runs[i].tags) ? C_TEXT : C_TEXT3;
        const int nw = text(String(i + 1), x, y, Font::Caption, C_TEXT3);
        text(runTime(v.runs[i].timeMs), x + nw + 4, y, Font::Caption, c);
        if (best) gfx->fillCircle(x - 6, y + 7, 2, C_GREEN);
      }
    }
  }

  // --- buttons: Start runs and New course, then Adjust start, Adjust finish and Back ---
  const int rowY1 = 152, rowH1 = 36, rowY2 = 194, rowH2 = 40, third = W / 3;
  const bool canStart = v.haveCourse;
  const bool canMake = v.gpsFix;
  auto btn = [&](int x, int y, int w, int h, const char* label, bool enabled, bool primary, bool hl) {
    gfx->fillRoundRect(x, y, w, h, CORNER, enabled && primary ? C_TEXT : hl ? C_RAISED : C_SURFACE);
    if (hl) gfx->drawRoundRect(x, y, w, h, CORNER, C_TEXT);
    const Font f = textWidth(label, Font::Button) <= w - 8 ? Font::Button : Font::Body;  // too wide for the button font
    text(fit(label, f, w - 8), x + w / 2, y + h / 2 + 1, f, !enabled ? C_TEXT3 : primary ? C_BG : C_TEXT, MC_DATUM);
  };
  btn(5, rowY1, 2 * third - 10, rowH1, "Start runs", canStart, true, highlight == 0);
  btn(2 * third + 5, rowY1, third - 10, rowH1, "New course", canMake, false, highlight == 1);
  const char* soon[2] = {"Adjust start", "Adjust finish"};
  for (int i = 0; i < 2; i++) {
    const int x = i * third + 5, w = third - 10;
    gfx->fillRoundRect(x, rowY2, w, rowH2, CORNER, highlight == 2 + i ? C_RAISED : C_SURFACE);
    if (highlight == 2 + i) gfx->drawRoundRect(x, rowY2, w, rowH2, CORNER, C_TEXT);
    text(soon[i], x + w / 2, rowY2 + 14, Font::Body, C_TEXT3, MC_DATUM);
    text("coming soon", x + w / 2, rowY2 + 29, Font::Caption, C_TEXT3, MC_DATUM);
  }
  btn(2 * third + 5, rowY2, third - 10, rowH2, "Back", true, false, highlight == 4);
  endFrame();
}

void DisplayManager::drawNewCourse(const GPSHandler& gps, bool ready, int highlight, bool touchOk) {
  beginFrame();
  header("New course", nullptr);

  gfx->fillRoundRect(8, HEADER_H + 8, W - 16, 112, CORNER + 2, C_SURFACE);
  centered("Stop at the start", HEADER_H + 18, Font::Title, C_YELLOW);
  centered("in the grid or at the lights,", HEADER_H + 52, Font::Body, C_TEXT);
  centered("then press Set start here", HEADER_H + 71, Font::Body, C_TEXT);
  centered(touchOk ? "The run starts as you move off" : "BOOT: press for the other, hold to choose", HEADER_H + 96, Font::Caption,
           C_TEXT2);

  // Speed, and whether it can go ahead
  char buf[24];
  snprintf(buf, sizeof(buf), "%.0f", Settings::speed(gps.lastFix().speedKmh));
  const int w = textWidth(buf, Font::NumL);
  text(buf, W / 2 - 14, 154, Font::NumL, C_TEXT, TC_DATUM);
  label(Settings::get().units == SpeedUnits::Mph ? "MPH" : "KM/H", W / 2 - 14 + w / 2 + 8, 154 + 37, BL_DATUM);
  const char* status = !gps.hasFix() ? "Waiting for a GPS fix…" : ready ? "Stopped: ready" : "Moving: stop first";
  text(status, 12, H - 48, Font::Caption, ready ? C_GREEN : C_YELLOW, BL_DATUM);

  const int third = W / 3, by = FOOTER_Y, bh = FOOTER_H;
  gfx->fillRoundRect(5, by, third - 10, bh, CORNER, highlight == 0 ? C_RAISED : C_SURFACE);
  if (highlight == 0) gfx->drawRoundRect(5, by, third - 10, bh, CORNER, C_TEXT);
  text("Back", 5 + (third - 10) / 2, by + bh / 2 + 1, Font::Button, C_TEXT, MC_DATUM);
  gfx->fillRoundRect(third + 5, by, 2 * third - 10, bh, CORNER, ready ? C_TEXT : highlight == 1 ? C_RAISED : C_SURFACE);
  if (highlight == 1) gfx->drawRoundRect(third + 5, by, 2 * third - 10, bh, CORNER, C_TEXT);
  text("Set start here", third + 5 + (2 * third - 10) / 2, by + bh / 2 + 1, Font::Button, ready ? C_BG : C_TEXT3, MC_DATUM);
  endFrame();
}
