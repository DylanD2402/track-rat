/**
 * Host-side tests for re-timing (Retime.h, SessionReplay::useLines): a
 * session timed again from its log with the track's lines as they are now.
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_retime.cpp src/VboFormat.cpp src/SessionReplay.cpp src/LapTimer.cpp -o /tmp/rt && /tmp/rt
 */

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "LapTimer.h"
#include "Retime.h"
#include "SessionReplay.h"
#include "VboFormat.h"
#include "check.h"

static const double SF_LAT = 45.0, SF_LON = -75.0, R = 150.0;
static double mPerDegLon() { return 111194.93 * cos(SF_LAT * geo::DEG2RAD); }

// A point on a 150 m circle through the start/finish (angle 0), anticlockwise.
static void circlePos(double angle, double& lat, double& lon) {
  lat = SF_LAT + R * sin(angle) / 111194.93;
  lon = SF_LON + R * (cos(angle) - 1.0) / mPerDegLon();
}
// The line at an angle round the circle, across the track, facing the way the car goes.
static TrackPoint lineAt(double angle) {
  TrackPoint p;
  circlePos(angle, p.lat, p.lon);
  p.radius = 15;
  p.heading = (float)fmod(360.0 - angle * 180.0 / M_PI + 360.0, 360.0);
  p.headingFrom = HeadingFrom::Set;
  return p;
}

// Five laps at 30 m/s (the third one quicker), logged with `sf` in the header.
static std::string makeLog(const TrackPoint& sf, const char* track) {
  vbo::Header h;
  h.year = 2026;
  h.month = 10;
  h.day = 18;
  h.utcMs = 36000000;
  h.comments = {"Track Rat GPS lap timer", "Started: 20261018_100000", "Type: laps", std::string("Track: ") + track,
                "Track name: Test Circle", "Session: 2", "Start/finish: " + vbo::pointText(sf), "Min lap ms: 20000"};
  std::string text = vbo::headerText(h);
  double angle = -0.3;
  for (int i = 0; angle < 2 * M_PI * 5 + 0.5; i++) {
    const int lap = (int)floor(angle / (2 * M_PI));
    const double v = lap == 2 ? 33.0 : 30.0;
    vbo::Sample s;
    s.utcMs = 36000000 + i * 50;
    circlePos(angle, s.lat, s.lon);
    s.speedKmh = (float)(v * 3.6);
    s.headingDeg = (float)fmod(360.0 - angle * 180.0 / M_PI + 720.0, 360.0);
    s.sats = 14;
    s.hdop = 0.6f;
    char buf[160];
    text.append(buf, vbo::formatLine(s, buf, sizeof(buf)));
    angle += v * 0.05 / R;
  }
  return text;
}

static void testReplayWithNewLines() {
  const TrackPoint oldLine = lineAt(0), newLine = lineAt(M_PI / 2);  // moved a quarter of the way round
  const std::string log = makeLog(oldLine, "circle");

  SessionReplay asLogged;
  asLogged.feed(log.data(), log.size());
  asLogged.finish();
  CHECK(asLogged.timed && asLogged.timer.lapCount() == 5, "as logged: 5 laps, got %zu", asLogged.timer.lapCount());

  SessionReplay moved;
  moved.summaryTrackId = "circle";
  moved.useLines(newLine, {}, {});
  moved.feed(log.data(), log.size());
  moved.finish();
  CHECK(moved.timed, "timed with the new line");
  CHECK(moved.timer.lapCount() == 4, "the log crosses the moved line 5 times: 4 laps, got %zu", moved.timer.lapCount());
  CHECK(fabs(moved.timer.startFinish().lat - newLine.lat) < 1e-9, "the new line, not the logged one");
  CHECK(moved.trackId == "circle" && moved.session == 2 && moved.stamp == "20261018_100000", "names kept");
  // A full lap of the circle is 942 m: 31.4 s at 30 m/s, 28.6 s at 33 m/s. With the line a quarter round,
  // the laps straddle the quick one differently: lap 2 is part slow, part quick
  const double full30 = 2 * M_PI * R / 30.0 * 1000, full33 = 2 * M_PI * R / 33.0 * 1000;
  const double mixed = full30 * 0.75 + full33 * 0.25;
  CHECK(fabs(asLogged.timer.lap(2).timeMs - full33) < 60, "as logged, lap 3 is the quick one: %u", asLogged.timer.lap(2).timeMs);
  CHECK(fabs(moved.timer.lap(2).timeMs - (full30 * 0.25 + full33 * 0.75)) < 60, "moved, lap 3 is mostly quick: %u",
        moved.timer.lap(2).timeMs);
  CHECK(fabs(moved.timer.lap(1).timeMs - mixed) < 60, "moved, lap 2 has the start of the quick lap: %u",
        moved.timer.lap(1).timeMs);
  CHECK(fabs(moved.timer.lap(3).timeMs - full30) < 60, "moved, lap 4 is slow: %u", moved.timer.lap(3).timeMs);
  CHECK(moved.timer.lap(2).timeMs != asLogged.timer.lap(2).timeMs, "the times changed");

  // A split added in the editor gets times on every lap
  SessionReplay withSplit;
  withSplit.useLines(oldLine, {lineAt(M_PI)}, {});
  withSplit.feed(log.data(), log.size());
  withSplit.finish();
  bool allSplits = withSplit.timer.lapCount() == 5;
  for (size_t i = 0; i < withSplit.timer.lapCount(); i++)
    allSplits = allSplits && withSplit.timer.lap(i).splitMs.size() == 1 && withSplit.timer.lap(i).splitMs[0] > 0;
  CHECK(allSplits, "the new split is timed on every lap");
  CHECK(fabs(withSplit.timer.lap(0).splitMs[0] - full30 / 2) < 60, "half way round: %d", withSplit.timer.lap(0).splitMs[0]);

  // Sectors too
  SessionReplay withSectors;
  withSectors.useLines(oldLine, {}, {lineAt(M_PI * 2 / 3), lineAt(M_PI * 4 / 3)});
  withSectors.feed(log.data(), log.size());
  withSectors.finish();
  CHECK(withSectors.timer.lapCount() == 5 && withSectors.timer.lap(1).sectorMs.size() == 2 &&
            withSectors.timer.lap(1).sectorMs[1] > withSectors.timer.lap(1).sectorMs[0],
        "sector gates timed");

  // A log with no line at all in its header can still be re-timed
  TrackPoint none = {0, 0, 0, 0, HeadingFrom::None};
  std::string bare = makeLog(none, "circle");
  size_t at = bare.find("Start/finish: ");
  bare.erase(at, bare.find("\r\n", at) + 2 - at);
  SessionReplay plain;
  plain.feed(bare.data(), bare.size());
  plain.finish();
  CHECK(!plain.timed, "no line in the header and no lookup: not timed");
  SessionReplay given;
  given.useLines(oldLine, {}, {});
  given.feed(bare.data(), bare.size());
  given.finish();
  CHECK(given.timed && given.timer.lapCount() == 5, "with the track's line: timed");

  // Another track's id in the request names the session after it
  SessionReplay renamed;
  renamed.summaryTrackId = "circle.short";
  renamed.useLines(oldLine, {}, {});
  renamed.feed(log.data(), log.size());
  renamed.finish();
  CHECK(renamed.trackId == "circle.short" && renamed.trackName.empty(), "named after the requested track");
  printf("  replay with the track's lines now: a moved line, a new split, sectors, no line in the log\n");
}

static void testRules() {
  using retime::Found;
  CHECK(retime::wanted("a.vbo", "x", "a.vbo", "y"), "one file: by name");
  CHECK(!retime::wanted("b.vbo", "y", "a.vbo", "y"), "one file: not another");
  CHECK(retime::wanted("b.vbo", "y", "", "y"), "all: by the summary's track");
  CHECK(!retime::wanted("b.vbo", "y.short", "", "y"), "all: another layout isn't this track");
  CHECK(!retime::wanted("b.vbo", "", "", ""), "no track: nothing");

  std::vector<Found> f = {{0, 20261017, 0}, {91000, 20261017, 1}, {90500, 20261018, 2}, {92000, 20261018, 3},
                          {89000, 20261016, 4}};
  Found b = retime::best(f);
  CHECK(b.lapMs == 89000 && b.session == 4, "best overall");
  Found d = retime::bestOfLatestDay(f);
  CHECK(d.lapMs == 90500 && d.date == 20261018, "best of the latest day");
  CHECK(retime::best({}).lapMs == 0 && retime::bestOfLatestDay({{0, 20261018, 0}}).lapMs == 0, "none");

  Found x = {90000, 20261018, 0};
  CHECK(retime::replacesPb(x, 0, false), "no PB: it's the PB");
  CHECK(retime::replacesPb(x, 88000, false), "a PB at another line can't be compared: replaced");
  CHECK(!retime::replacesPb(x, 88000, true), "a quicker PB at this line stays");
  CHECK(retime::replacesPb(x, 91000, true), "quicker than the PB");
  CHECK(!retime::replacesPb(Found(), 0, false), "no clean lap: nothing to save");

  CHECK(retime::replacesDay(x, 0, 0, false), "no day best");
  CHECK(retime::replacesDay(x, 85000, 20261017, true), "a later day replaces an earlier one");
  CHECK(!retime::replacesDay(x, 95000, 20261019, true), "a stored lap from a later day stays");
  CHECK(!retime::replacesDay(x, 95000, 20261019, false), "even at another line");
  CHECK(retime::replacesDay(x, 85000, 20261018, false), "same day, another line: replaced");
  CHECK(!retime::replacesDay(x, 85000, 20261018, true), "same day, quicker stays");
  CHECK(retime::replacesDay(x, 95000, 20261018, true), "same day, slower is replaced");
  printf("  rules: which logs, the PB and the best of the day\n");
}

int main() {
  printf("Re-timing tests\n");
  testReplayWithNewLines();
  testRules();
  if (failures) {
    printf("\n%d FAILURE(S)\n", failures);
    return 1;
  }
  printf("\nALL PASSED\n");
  return 0;
}
