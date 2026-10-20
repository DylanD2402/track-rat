/**
 * Host-side tests for the timing screen's rules (TimingScreen.h): colours,
 * the trend bar, the sector strip, the flash at the line, the ghost and taps.
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_timing.cpp src/LapTimer.cpp -o /tmp/tm && /tmp/tm
 */

#include <cmath>
#include <cstdio>
#include <vector>
#include "LapTimer.h"
#include "TimingScreen.h"
#include "check.h"

using namespace timing;

static void testColours() {
  CHECK(deltaColour(-250, DeltaColours::GreenRed) == ui::C_GREEN, "ahead is green");
  CHECK(deltaColour(250, DeltaColours::GreenRed) == ui::C_RED, "behind is red");
  CHECK(deltaColour(0, DeltaColours::GreenRed) == ui::C_GREEN, "level counts as ahead");
  CHECK(deltaColour(-250, DeltaColours::BlueOrange) == ui::C_BLUE, "ahead is blue");
  CHECK(deltaColour(250, DeltaColours::BlueOrange) == ui::C_ORANGE, "behind is orange");
  for (DeltaColours c : {DeltaColours::GreenRed, DeltaColours::BlueOrange}) {
    Colours k = colours(c);
    CHECK(k.ahead != k.behind, "ahead and behind differ");
    CHECK(k.dayBest != ui::C_PURPLE && k.dayBest != ui::C_YELLOW, "the day's best isn't a PB or slower");
    // Never red against green in the blue / orange set
    if (c == DeltaColours::BlueOrange)
      CHECK(k.ahead != ui::C_GREEN && k.behind != ui::C_RED && k.dayBest != ui::C_GREEN, "no green or red");
  }
  printf("  colours: green / red, blue / orange, level is ahead\n");
}

static void testTrend() {
  DeltaTrend t;
  CHECK(t.change() == NO_TREND, "nothing yet");
  // Gaining 100 ms a second, at 20 Hz
  uint32_t ms = 100000;
  int32_t d = 500;
  for (int i = 0; i < 10; i++, ms += 50, d -= 5) t.add(ms, d, true);
  CHECK(t.change() == NO_TREND, "under a second of history: no trend yet, got %d", t.change());
  for (int i = 0; i < 200; i++, ms += 50, d -= 5) t.add(ms, d, true);  // 10 s more
  int32_t c = t.change();
  CHECK(c <= -280 && c >= -310, "gaining 100 ms/s over 3 s, got %d", c);
  CHECK(trendFraction(c) > 0.9f, "the bar is nearly full height, up: %f", trendFraction(c));

  // Then losing 50 ms a second
  for (int i = 0; i < 100; i++, ms += 50, d += 2) t.add(ms, d, true);  // 5 s, 40 ms/s
  c = t.change();
  CHECK(c >= 110 && c <= 130, "losing 40 ms/s over 3 s, got %d", c);
  CHECK(trendFraction(c) < -0.3f && trendFraction(c) > -0.5f, "the bar goes down: %f", trendFraction(c));

  // Steady
  for (int i = 0; i < 100; i++, ms += 50) t.add(ms, d, true);
  CHECK(t.change() == 0 && trendFraction(t.change()) == 0, "steady: %d", t.change());

  // Off the reference line: starts again
  t.add(ms, 0, false);
  CHECK(t.change() == NO_TREND, "cleared without a delta");
  // A gap in fixes: old samples are dropped
  for (int i = 0; i < 40; i++, ms += 50) t.add(ms, 100, true);
  ms += 20000;
  t.add(ms, 900, true);
  CHECK(t.change() == NO_TREND, "after a long gap there's no history, got %d", t.change());
  // Time going backwards (a new day) starts again
  for (int i = 0; i < 40; i++, ms += 50) t.add(ms, 100, true);
  t.add(1000, 100, true);
  CHECK(t.change() == NO_TREND, "time went backwards");
  // Fixes faster than the sample rate keep the newest value
  t.clear();
  t.add(0, 0, true);
  t.add(1000, 0, true);
  t.add(1050, 80, true);
  CHECK(t.change() == 80, "the newest value counts, got %d", t.change());
  // Clamped
  CHECK(trendFraction(-5000) == 1 && trendFraction(5000) == -1 && trendFraction(NO_TREND) == 0, "clamped");
  printf("  trend bar: gaining up, losing down, cleared by gaps and lost deltas\n");
}

static void testSectorStrip() {
  CHECK(sectorMark(-1, 10000, 10000) == Mark::Missed, "no time");
  CHECK(sectorMark(9000, 10000, 9500) == Mark::Ever, "beats the best ever");
  CHECK(sectorMark(10000, 10000, 10000) == Mark::Ever, "equals the best ever");
  CHECK(sectorMark(9800, 9500, 10000) == Mark::Day, "best of the day");
  CHECK(sectorMark(10100, 9500, 10000) == Mark::Slower, "slower");
  CHECK(sectorMark(10100, -1, 10500) == Mark::Ever, "nothing ever: it's the best");
  CHECK(sectorMark(10100, 9000, -1) == Mark::Day, "nothing today: it's today's best");

  std::vector<int32_t> a = {9000, -1, 12000, 0}, b = {9500, 8000, -1};
  std::vector<int32_t> m = bestOf(a, b);
  CHECK(m.size() == 4 && m[0] == 9000 && m[1] == 8000 && m[2] == 12000 && m[3] == -1, "bestOf %d %d %d %d", m[0], m[1],
        m[2], m[3]);

  const std::vector<int32_t> ever = {10000, 15000, 20000, 9000}, day = {10500, 15500, 21000, 9500};
  // No sectors at all
  CHECK(sectorStrip({}, ever, day).size() == 1, "no gates: one sector, the whole lap");
  // Nothing crossed yet
  std::vector<Mark> s = sectorStrip({-1, -1, -1}, ever, day);
  CHECK(s.size() == 4 && s[0] == Mark::Pending && s[3] == Mark::Pending, "all pending");
  // T1 a PB, T2 best of the day, T3 slower; T4 ends at the line
  s = sectorStrip({9900, 9900 + 15200, 9900 + 15200 + 22000}, ever, day);
  CHECK(s[0] == Mark::Ever && s[1] == Mark::Day && s[2] == Mark::Slower && s[3] == Mark::Pending, "%d %d %d %d", (int)s[0],
        (int)s[1], (int)s[2], (int)s[3]);
  // T2's gate missed: T2 and T3 can't be timed, T1 can
  s = sectorStrip({9900, -1, 47000}, ever, day);
  CHECK(s[0] == Mark::Ever && s[1] == Mark::Missed && s[2] == Mark::Missed && s[3] == Mark::Pending, "missed gate");
  // Only T1 so far: the rest are pending, not missed
  s = sectorStrip({11000, -1, -1}, ever, day);
  CHECK(s[0] == Mark::Slower && s[1] == Mark::Pending && s[2] == Mark::Pending, "under way");
  // Out of order (a glitch): missed, never a negative time
  s = sectorStrip({11000, 10000, -1}, ever, day);
  CHECK(s[1] == Mark::Missed, "a gate before the one before it");
  // Fewer bests than sectors (a layout gained a sector): best there is
  s = sectorStrip({11000, 30000}, {}, {});
  CHECK(s[0] == Mark::Ever && s[1] == Mark::Ever, "no bests at all");

  CHECK(markColour(Mark::Ever, DeltaColours::GreenRed) == ui::C_PURPLE, "purple");
  CHECK(markColour(Mark::Day, DeltaColours::GreenRed) == ui::C_GREEN, "green");
  CHECK(markColour(Mark::Day, DeltaColours::BlueOrange) == ui::C_BLUE, "blue");
  CHECK(markColour(Mark::Slower, DeltaColours::GreenRed) == ui::C_YELLOW, "yellow");
  CHECK(markColour(Mark::Pending, DeltaColours::GreenRed) == ui::C_SURFACE, "pending is empty");
  printf("  sector strip: purple, green, yellow, missed gates, pending\n");
}

static void testLineFlash() {
  CHECK(lineFlash(true, true) == LineFlash::Pb && lineFlash(true, false) == LineFlash::Pb, "a PB is a PB");
  CHECK(lineFlash(false, true) == LineFlash::DayBest, "best of the day");
  CHECK(lineFlash(false, false) == LineFlash::Lap, "any other lap");
  CHECK(flashColour(LineFlash::Pb, DeltaColours::GreenRed) == ui::C_PURPLE, "purple PB");
  CHECK(flashColour(LineFlash::Pb, DeltaColours::BlueOrange) == ui::C_PURPLE, "purple PB with either palette");
  CHECK(flashColour(LineFlash::DayBest, DeltaColours::GreenRed) == ui::C_GREEN, "green day best");
  CHECK(flashColour(LineFlash::DayBest, DeltaColours::BlueOrange) == ui::C_BLUE, "blue day best");
  CHECK(flashTextColour(LineFlash::Pb) == ui::C_BG && flashTextColour(LineFlash::Lap) == ui::C_TEXT, "text colours");
  printf("  line flash: purple PB, green (blue) best of the day\n");
}

static void testGhost() {
  std::vector<LapTimer::TracePt> path = {{0, 0, 0, 100}, {10, 0, 1000, 100}, {10, 20, 2000, 100}, {0, 0, 3000, 100}};
  float x = -1, y = -1;
  CHECK(ghostAt(path, 500, x, y) && fabsf(x - 5) < 1e-4f && fabsf(y) < 1e-4f, "halfway along the first leg: %f %f", x, y);
  CHECK(ghostAt(path, 1500, x, y) && fabsf(x - 10) < 1e-4f && fabsf(y - 10) < 1e-4f, "second leg: %f %f", x, y);
  CHECK(ghostAt(path, 2000, x, y) && x == 10 && y == 20, "on a point");
  CHECK(ghostAt(path, 0, x, y) && x == 0 && y == 0, "at the line");
  CHECK(ghostAt(path, 3000, x, y), "at the end");
  CHECK(!ghostAt(path, 3001, x, y), "past the reference lap's time: no ghost");
  CHECK(!ghostAt(path, -1, x, y), "before the start");
  CHECK(!ghostAt(std::vector<LapTimer::TracePt>(path.begin(), path.begin() + 1), 0, x, y), "one point is no path");
  std::vector<LapTimer::TracePt> same = {{0, 0, 100, 0}, {5, 5, 100, 0}};  // two fixes with one time
  CHECK(ghostAt(same, 100, x, y) && x == 0 && y == 0, "no divide by zero");

  // A real lap timer: the ghost of the reference lap, from the session's own best lap
  LapTimer lt;
  TrackPoint sf = {45.0, -75.0, 15, 90, HeadingFrom::Set};
  lt.begin(sf, {}, 20000);
  CHECK(lt.referencePath().empty(), "no reference yet");
  CHECK(lt.currentSectorMs().empty(), "no lap under way");
  // Drive east through the line, round a 500 m square, three laps at 25 m/s
  const double R = 6371000.0, deg = M_PI / 180;
  auto at = [&](double s, double& lat, double& lon) {
    double side = fmod(s, 2000.0);
    if (side < 0) side += 2000;
    double x, y;
    if (side < 250) x = side, y = 0;
    else if (side < 750) x = 250, y = -(side - 250);
    else if (side < 1250) x = 250 - (side - 750), y = -500;
    else if (side < 1750) x = -250, y = -500 + (side - 1250);
    else x = -250 + (side - 1750), y = 0;
    lat = 45.0 + y / R / deg;
    lon = -75.0 + x / (R * cos(45.0 * deg)) / deg;
  };
  uint32_t t = 0;
  for (double s = -100; s < 2000 * 2 + 300; s += 25 * 0.05, t += 50) {
    double la, lo;
    at(s, la, lo);
    lt.onFix(la, lo, 90, t, t);
  }
  CHECK(lt.lapCount() == 2, "two laps, got %zu", lt.lapCount());
  CHECK(lt.referencePath().size() > 100, "the best lap is the reference");
  CHECK(ghostAt(lt.referencePath(), 2000, x, y) && fabsf(x - 50) < 3 && fabsf(y) < 3, "2 s in: 50 m along, got %f %f", x, y);
  printf("  ghost: interpolated along the reference lap, none past its ends\n");
}

static void testTaps() {
  CHECK(tapSwitchesView(true, 0, 50), "parked: a quick tap counts");
  CHECK(tapSwitchesView(true, TAP_MOVING_KMH - 1, 50), "crawling: counts");
  CHECK(!tapSwitchesView(true, 120, 80), "moving: a quick tap is ignored");
  CHECK(!tapSwitchesView(true, TAP_MOVING_KMH, FIRM_PRESS_MS - 1), "just too quick");
  CHECK(tapSwitchesView(true, 120, FIRM_PRESS_MS), "moving: a firm press counts");
  CHECK(tapSwitchesView(false, 120, 50), "no fix: the speed is stale, the tap counts");
  printf("  taps: quick ones ignored while moving, firm presses count\n");
}

int main() {
  printf("Timing screen tests\n");
  testColours();
  testTrend();
  testSectorStrip();
  testLineFlash();
  testGhost();
  testTaps();
  if (failures) {
    printf("\n%d FAILURE(S)\n", failures);
    return 1;
  }
  printf("\nALL PASSED\n");
  return 0;
}
