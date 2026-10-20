/**
 * Track Rat - the pit debrief screens (theoretical best, where the time went)
 *
 * Part of DisplayManager; kept in its own file so the other screens stay as they are.
 * The numbers are worked out in Debrief.h; this only lays them out.
 */

#include "DisplayManager.h"
#include "Config.h"
#include "SessionLog.h"
#include "Settings.h"
#include "UiTheme.h"

using namespace ui;
using Font = DisplayManager::Font;

static const uint16_t MARK_COLORS[3] = {C_RED, C_ORANGE, C_YELLOW};

static String secs(int32_t ms) { return String(debrief::seconds(ms).c_str()); }

void DisplayManager::drawDebrief(const Track& track, const LapTimer& timer, const DebriefView& d, int page) {
  beginFrame();
  const debrief::Result& r = d.r;
  String title = d.session > 0 ? "Session " + String(d.session) : String("In the pits");
  if (page == 0 && d.session > 0) title = "Pits   ·   " + title;
  header(title + "   ·   " + (page == 0 ? track.displayName() : String("laps and sectors")), nullptr);
  char buf[64];

  if (page == 0) {
    // Best, theoretical best and the gap between them, across the top
    const int y = HEADER_H + 4;
    label("BEST LAP", 10, y);
    label("THEORETICAL", 116, y);
    label("ON THE TABLE", 222, y);
    text(formatLapTime(r.bestMs), 10, y + 14, Font::Title, C_GREEN);
    text(formatLapTime(r.theoreticalMs), 116, y + 14, Font::Title, C_TEXT);
    text(secs((int32_t)(r.bestMs - r.theoreticalMs)) + " s", 222, y + 14, Font::Title, C_YELLOW);
    String used = "Laps used: " + String(r.lapsUsed);
    if (r.lapsLeftOut) used += ", " + String(r.lapsLeftOut) + " left out";
    if (d.allTimeTheoreticalMs) used += "   ·   ever: " + formatLapTime(d.allTimeTheoreticalMs);
    const int temps = tempMaxLine(timer, W - 10, y + 46 + 7);  // the engine's hottest (6.5), on the right
    text(fit(used, Font::Caption, W - 20 - (temps ? temps + 12 : 0)), 10, y + 46, Font::Caption, C_TEXT3);

    // The best lap's path, with the sectors that cost the most marked 1, 2, 3
    const int my = HEADER_H + 66, mh = 116, mw = 130;
    std::vector<MapMark> marks;
    const std::vector<LapTimer::TracePt>& path = timer.bestPath();
    const bool pathIsBest = timer.bestPathLapIndex() == r.bestIndex;
    if (pathIsBest && r.bestIndex >= 0 && r.bestIndex < (int)timer.lapCount() &&
        timer.lap(r.bestIndex).sectorMs.size() + 1 == r.sectors) {
      const std::vector<int32_t>& gates = timer.lap(r.bestIndex).sectorMs;
      for (size_t k = 0; k < r.top.size(); k++) {
        const int s = r.top[k];
        const int32_t from = s == 0 ? 0 : gates[s - 1];
        const int32_t to = s < (int)gates.size() ? gates[s] : (int32_t)r.bestMs;
        size_t a = 0, b = 0;
        for (size_t i = 0; i < path.size(); i++) {
          if (path[i].t <= from) a = i;
          if (path[i].t <= to) b = i;
        }
        marks.push_back({a, b, MARK_COLORS[k], (int)k + 1});
      }
    }
    MapPath mp = {(const uint8_t*)path.data(), path.size(), sizeof(LapTimer::TracePt)};
    if (!drawMapArea(4, my, mw, mh, track.map, mp, false, {0, 0}, false, track.hasStartFinish, &marks))
      text("No map", 4 + mw / 2, my + mh / 2, Font::Caption, C_TEXT3, MC_DATUM);

    // The three sectors, biggest loss first
    int ry = my + 2;
    const int rx = 140;
    if (r.top.empty()) {
      text("Nothing lost in any sector:", rx, ry + 8, Font::Body, C_TEXT);
      text("your best lap is your", rx, ry + 28, Font::Body, C_TEXT);
      text("theoretical best", rx, ry + 48, Font::Body, C_TEXT);
    }
    for (size_t k = 0; k < r.top.size(); k++, ry += 39) {
      const int s = r.top[k];
      gfx->fillCircle(rx + 7, ry + 8, 7, MARK_COLORS[k]);
      text(String((int)k + 1), rx + 7, ry + 9, Font::Caption, C_BG, MC_DATUM);
      snprintf(buf, sizeof(buf), "T%d  %s s", s + 1, debrief::seconds(r.gaps[s]).c_str());
      text(buf, rx + 20, ry, Font::Body, C_TEXT);
      snprintf(buf, sizeof(buf), "best on lap %u", (unsigned)r.best[s].lap);
      text(buf, rx + 20, ry + 17, Font::Caption, C_TEXT2);
      if (d.hints[k].size()) text(fit(d.hints[k].c_str(), Font::Caption, W - rx - 22), rx + 20, ry + 28, Font::Caption, C_TEXT3);
    }
    centered("Drive off to carry on   ·   Tap: more   ·   Hold: end", H - 18, Font::Caption, C_TEXT3);
    endFrame();
    return;
  }

  // Page 1: every lap (newest first) on the left, every sector on the right. With engine
  // temperatures, each lap's hottest coolant in a narrow column before the tag.
  const int top = HEADER_H + 6, rowH = 13, rows = 12;
  const bool temps = hasTemp(timer.sessionCoolantMaxC());
  const bool f = settings::fahrenheit(Settings::get());
  const int tagX = temps ? 112 : 100;
  label("LAPS", 10, top);
  if (temps) label("MAX", 84, top);
  int y = top + 14;
  for (int i = (int)timer.lapCount() - 1, n = 0; i >= 0 && n < rows; i--, n++, y += rowH) {
    const Lap& l = timer.lap(i);
    const bool best = i == r.bestIndex;
    snprintf(buf, sizeof(buf), "%u", (unsigned)l.number);
    text(buf, 10, y, Font::Caption, C_TEXT2);
    text(formatLapTime(l.timeMs), temps ? 30 : 34, y, Font::Caption, best ? C_GREEN : C_TEXT);
    if (temps && hasTemp(l.coolantMaxC)) {
      snprintf(buf, sizeof(buf), "%d\xC2\xB0", tempalert::inUnits(l.coolantMaxC, f));
      text(buf, 108, y, Font::Caption, tempColor(l.coolantMaxC, false), TR_DATUM);
    }
    const char* tag = best ? "BEST" : laptags::shortLabel(l.tags);
    text(temps ? fit(tag, Font::Caption, 154 - tagX) : String(tag), tagX, y, Font::Caption, best ? C_GREEN : laptags::suspect(l.tags) ? C_ORANGE : C_YELLOW);
  }
  gfx->drawFastVLine(156, top, rows * rowH + 12, C_LINE);
  label("BEST", 196, top);
  label("LAP", 246, top);
  label("LOST", 274, top);
  y = top + 14;
  for (size_t s = 0; s < r.sectors && (int)s < rows; s++, y += rowH) {
    snprintf(buf, sizeof(buf), "T%u", (unsigned)(s + 1));
    text(buf, 164, y, Font::Caption, C_TEXT2);
    text(secs(r.best[s].ms), 196, y, Font::Caption, C_TEXT);
    text(String((unsigned)r.best[s].lap), 250, y, Font::Caption, C_TEXT2);
    if (s < r.gaps.size() && r.gaps[s] > 0) text("+" + secs(r.gaps[s]), 276, y, Font::Caption, C_YELLOW);
  }
  if ((int)r.sectors > rows) text("and " + String((int)r.sectors - rows) + " more", 164, y, Font::Caption, C_TEXT3);

  // This session against the last one here
  String vs;
  if (d.lastSession > 0 && d.lastBestMs > 0) {
    const int32_t diff = (int32_t)d.lastBestMs - (int32_t)r.bestMs;
    String which = d.lastToday ? "session " + String(d.lastSession) : String("last time");
    vs = "Best " + secs(diff < 0 ? -diff : diff) + " s " + (diff >= 0 ? "quicker" : "slower") + " than " + which;
  } else {
    vs = "First session here with a timed lap";
  }
  centered(fit(vs, Font::Body, W - 16), H - 42, Font::Body, C_TEXT);
  centered("Tap: timing screen   ·   Hold: end the session", H - 18, Font::Caption, C_TEXT3);
  endFrame();
}
