/**
 * Host-side tests for automatic sessions and lap tags (no ESP32 needed):
 * a simulated day at the track with pit stops, a red flag, a power cut and
 * glitchy laps, through the real LapTimer, PitWatch and SessionReplay.
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_sessions.cpp src/LapTimer.cpp src/VboFormat.cpp src/SessionReplay.cpp -o /tmp/ses && /tmp/ses
 */

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "AutoStart.h"
#include "LapTags.h"
#include "LapTimer.h"
#include "SessionReplay.h"
#include "VboFormat.h"
#include "check.h"

static const double SF_LAT = 45.0, SF_LON = -76.0, R = 150.0;  // a 942 m circle, S/F at its easternmost point
static const double M_PER_DEG_LAT = 111194.93;
static const double TWO_PI = 2 * M_PI;

static void circlePos(double angle, double& lat, double& lon) {
  lat = SF_LAT + R * sin(angle) / M_PER_DEG_LAT;
  lon = SF_LON + R * (cos(angle) - 1.0) / (M_PER_DEG_LAT * cos(SF_LAT * geo::DEG2RAD));
}

// Drives a car round the circle at 10 Hz, feeding every fix to the lap timer
// and the pit watch, and keeping the log a session would write.
struct Driver {
  LapTimer lt;
  PitWatch pits;
  uint32_t t;       // GPS time, ms since UTC midnight
  double angle = -0.3;
  std::vector<PitWatch::Event> pitEvents;
  std::vector<LapEvent> lapEvents;
  std::vector<vbo::Sample> log;
  uint32_t lastLogged = 0;

  explicit Driver(uint32_t startMs) : t(startMs) {
    lt.begin({SF_LAT, SF_LON, 15, 0, HeadingFrom::Set}, {});  // anticlockwise: north at the S/F
    pits.reset();
  }

  void fix(double lat, double lon, float kmh, float hAcc = -1) {
    for (const LapEvent& e : lt.onFix(lat, lon, kmh, t, t, hAcc)) lapEvents.push_back(e);
    PitWatch::Event pe = pits.onFix(kmh, t);
    if (pe != PitWatch::Event::None) pitEvents.push_back(pe);
    vbo::Sample s;  // as SessionLog::logFix writes it
    s.utcMs = t;
    s.lat = lat;
    s.lon = lon;
    s.speedKmh = kmh;
    s.sats = 12;
    s.lap = lt.lapRunning() ? lt.currentLapNumber() : 0;
    s.tag = laptags::fixFlags(kmh, hAcc, log.empty() ? 0 : vbo::utcDiffMs(lastLogged, t));
    log.push_back(s);
    lastLogged = t;
    t = (t + 100) % 86400000u;
  }

  // Round the circle to `to` (radians, increasing) at v m/s.
  void arc(double to, double v, float hAcc = -1) {
    while (angle < to) {
      double lat, lon;
      circlePos(angle, lat, lon);
      fix(lat, lon, (float)(v * 3.6), hAcc);
      angle += v * 0.1 / R;
    }
  }
  // Straight across from the current angle to `to` (a cut across the infield).
  void chord(double to, double v) {
    double la0, lo0, la1, lo1;
    circlePos(angle, la0, lo0);
    circlePos(to, la1, lo1);
    double len = 2 * R * sin((to - angle) / 2);
    int steps = (int)(len / (v * 0.1));
    for (int i = 0; i < steps; i++) {
      double f = (double)i / steps;
      fix(la0 + f * (la1 - la0), lo0 + f * (lo1 - lo0), (float)(v * 3.6));
    }
    angle = to;
  }
  // Parked where it is for `ms`, GPS still running (a little wander).
  void stop(uint32_t ms) {
    double lat, lon;
    circlePos(angle, lat, lon);
    for (uint32_t k = 0; k < ms / 100; k++) fix(lat + (k % 3) * 1e-6, lon, (float)(k % 4) * 0.5f);
  }
  // No fixes for `ms` while the car carries on at v m/s.
  void gap(uint32_t ms, double v) {
    t = (t + ms) % 86400000u;
    angle += v * ms / 1000.0 / R;
  }

  int count(PitWatch::Event e) const {
    int n = 0;
    for (PitWatch::Event x : pitEvents) n += x == e;
    return n;
  }
};

static const char* tagText(uint8_t tags) {
  static char buf[64];
  snprintf(buf, sizeof(buf), "%s%s%s%s%s", tags & laptags::PIT ? "pit " : "", tags & laptags::SLOW ? "slow " : "",
           tags & laptags::GPS_GAP ? "gap " : "", tags & laptags::POOR_ACCURACY ? "accuracy " : "",
           tags & laptags::ODD_DISTANCE ? "distance " : "");
  return buf;
}

// Times the session again from its log, as the boot recovery does after a power cut.
static void replay(const Driver& d, SessionReplay& r, int session) {
  vbo::Header h;
  h.comments.push_back("Type: laps");
  h.comments.push_back("Track: circle");
  h.comments.push_back("Started: 20261020_091500");
  h.comments.push_back("Session: " + std::to_string(session));
  h.comments.push_back("Start/finish: " + vbo::pointText(d.lt.startFinish()));
  h.comments.push_back("Min lap ms: " + std::to_string(MIN_LAP_TIME_MS));
  std::string text = vbo::headerText(h);
  char line[160];
  for (const vbo::Sample& s : d.log) text.append(line, vbo::formatLine(s, line, sizeof(line)));
  r.feed(text.data(), text.size());
  r.finish();
}

// The morning: a clean lap, a best, a red flag, then every kind of glitch,
// a cool-down lap, and into the pits for good.
static void testMorningSession() {
  Driver d(9 * 3600000u + 15 * 60000u);
  d.arc(0.0, 25);                        // out lap to the line: lap 1 starts
  d.arc(TWO_PI, 30);                     // lap 1: clean
  d.arc(2 * TWO_PI, 31);                 // lap 2: clean, the best
  d.arc(2 * TWO_PI + M_PI, 31);          // lap 3: red flag halfway round...
  d.stop(5 * 60000);                     // ...5 minutes on track...
  d.arc(3 * TWO_PI, 31);                 // ...and off again: same session
  d.arc(3 * TWO_PI + 2, 32);             // lap 4: a 1.5 s GPS gap, otherwise the fastest yet
  d.gap(1500, 32);
  d.arc(4 * TWO_PI, 32);
  d.arc(4 * TWO_PI + 1, 32.5);           // lap 5: a patch of poor accuracy
  d.arc(4 * TWO_PI + 1.5, 32.5, 8.0f);
  d.arc(5 * TWO_PI, 32.5, 1.5f);
  d.arc(5 * TWO_PI + M_PI / 2, 31);      // lap 6: cuts across the infield - shorter, so quicker
  d.chord(5 * TWO_PI + 3 * M_PI / 2, 31);
  d.arc(6 * TWO_PI, 31);
  d.arc(7 * TWO_PI, 26);                 // lap 7: cool-down, slow
  d.arc(8 * TWO_PI, 30.5);               // lap 8: clean
  d.arc(8 * TWO_PI + 0.5, 15);           // into the pits...
  d.stop(12 * 60000);                    // ...and parked for good

  const LapTimer& lt = d.lt;
  CHECK(lt.lapCount() == 8, "8 laps, got %zu", lt.lapCount());
  if (lt.lapCount() != 8) return;
  const uint8_t want[8] = {0, 0, laptags::PIT | laptags::SLOW, laptags::GPS_GAP, laptags::POOR_ACCURACY,
                           laptags::ODD_DISTANCE, laptags::SLOW, 0};
  for (size_t i = 0; i < 8; i++) {
    const Lap& l = lt.lap(i);
    std::string got = tagText(l.tags);
    CHECK(l.tags == want[i], "lap %zu (%.3f s, %.0f m): tags %s, want %s", i + 1, l.timeMs / 1000.0, l.distanceM,
          got.c_str(), tagText(want[i]));
    printf("  lap %zu: %7.3f s %5.0f m  %s\n", i + 1, l.timeMs / 1000.0, l.distanceM, got.c_str());
  }
  CHECK(lt.bestLapIndex() == 1, "best is lap 2, got lap %d", lt.bestLapIndex() + 1);
  CHECK(lt.lap(3).timeMs < lt.lap(1).timeMs && lt.lap(5).timeMs < lt.lap(1).timeMs,
        "the gap lap and the cut lap were quicker than the best (%u, %u vs %u)", lt.lap(3).timeMs, lt.lap(5).timeMs,
        lt.lap(1).timeMs);
  for (const LapEvent& e : d.lapEvents)
    if (e.type == LapEventType::LapCompleted && e.isBest)
      CHECK(e.timeMs == lt.lap(0).timeMs || e.timeMs == lt.lap(1).timeMs, "only laps 1 and 2 flash best (%u)", e.timeMs);
  CHECK(lt.untaggedLapCount() == 3, "3 untagged laps, got %zu", lt.untaggedLapCount());
  uint32_t avg = (lt.lap(0).timeMs + lt.lap(1).timeMs + lt.lap(7).timeMs) / 3;
  CHECK(lt.averageLapMs() == avg, "average of laps 1, 2, 8: %u, got %u", avg, lt.averageLapMs());

  // Red flag: parked a minute in, off again before 10 minutes. Then parked for good.
  CHECK(d.count(PitWatch::Event::Parked) == 2, "parked twice, got %d", d.count(PitWatch::Event::Parked));
  CHECK(d.count(PitWatch::Event::MovedOff) == 1, "moved off once, got %d", d.count(PitWatch::Event::MovedOff));
  CHECK(d.count(PitWatch::Event::SessionOver) == 1, "session over once, got %d", d.count(PitWatch::Event::SessionOver));
  CHECK(d.pitEvents.size() == 4 && d.pitEvents.back() == PitWatch::Event::SessionOver, "session over last");
  printf("  red flag: parked, carried on; pits: parked, session over after 10 min\n");

  // A power cut: the summary is rebuilt from the log with the same tags
  SessionReplay r;
  replay(d, r, 1);
  CHECK(r.session == 1, "replay: session %d", r.session);
  CHECK(r.timer.lapCount() == lt.lapCount(), "replay: %zu laps", r.timer.lapCount());
  for (size_t i = 0; i < r.timer.lapCount() && i < lt.lapCount(); i++)
    CHECK(r.timer.lap(i).tags == lt.lap(i).tags, "replay lap %zu: tags %s", i + 1, tagText(r.timer.lap(i).tags));
  CHECK(r.timer.bestLapIndex() == 1, "replay: best is lap %d", r.timer.bestLapIndex() + 1);
  printf("  rebuilt from the log: same laps, tags and best\n");
}

// Pit timing on its own: a minute to count as parked, 10 minutes from stopping to end the session.
static void testPitWatch() {
  PitWatch p;
  p.reset();
  uint32_t t = 86400000u - 30000;  // across midnight too
  auto run = [&](float kmh, uint32_t ms) {
    std::vector<std::pair<PitWatch::Event, uint32_t>> evs;
    for (uint32_t k = 0; k < ms; k += 100) {
      PitWatch::Event e = p.onFix(kmh, t);
      if (e != PitWatch::Event::None) evs.push_back({e, k});
      t = (t + 100) % 86400000u;
    }
    return evs;
  };
  CHECK(run(80, 5000).empty(), "driving: nothing");
  CHECK(run(3, 59000).empty() && !p.parked(), "59 s slow: not parked yet");
  CHECK(run(30, 1000).empty(), "moving again resets the count");
  auto e = run(2, 61000);
  CHECK(e.size() == 1 && e[0].first == PitWatch::Event::Parked && e[0].second == 60000, "parked after 60 s");
  CHECK(p.parked(), "parked");
  CHECK(run(10, 5000).empty(), "creeping in the paddock is still parked");
  e = run(40, 3000);
  CHECK(e.size() == 1 && e[0].first == PitWatch::Event::MovedOff && e[0].second == 2000, "drive off after 2 s fast");
  CHECK(!p.parked(), "driving again");
  e = run(0, 11 * 60000);
  CHECK(e.size() == 2 && e[0].first == PitWatch::Event::Parked && e[0].second == 60000, "parked again");
  CHECK(e.size() == 2 && e[1].first == PitWatch::Event::SessionOver && e[1].second == 10 * 60000,
        "session over 10 min after stopping (%u)", e.size() == 2 ? e[1].second : 0);
  CHECK(run(80, 5000).empty(), "once over, the pit watch is quiet until reset");
  p.reset();
  CHECK(!p.parked(), "reset: driving");
  printf("  pit watch: 60 s to park, drive off carries on, 10 min from stopping ends it, across midnight\n");
}

// Session numbers through the day, across a power cut, a new day and another track.
static void testSessionNumbers() {
  SessionCount flash;  // what's in NVS
  CHECK(nextSessionNumber(flash, "calabogie", "20261020_091500") == 1, "first of the day");
  CHECK(nextSessionNumber(flash, "calabogie", "20261020_104000") == 2, "after the pits");
  SessionCount afterPowerCut = flash;  // read back at boot
  CHECK(nextSessionNumber(afterPowerCut, "calabogie", "20261020_131000") == 3, "after a power cut");
  CHECK(nextSessionNumber(afterPowerCut, "calabogie", "20261021_090000") == 1, "next day");
  CHECK(nextSessionNumber(afterPowerCut, "shannonville", "20261021_120000") == 1, "another track");
  CHECK(nextSessionNumber(afterPowerCut, "calabogie", "20261021_150000") == 1, "back again: counts again");
  CHECK(nextSessionNumber(afterPowerCut, "calabogie", "") == 1, "no GPS date");
  CHECK(nextSessionNumber(afterPowerCut, "calabogie", "") == 1, "still no GPS date");
  CHECK(nextSessionNumber(afterPowerCut, "bench", "SIM_42") == 1, "bench test");
  printf("  session numbers: 1, 2, 3 after a power cut; 1 on a new day, at another track, with no date\n");
}

// The tags and the best lap from laps alone.
static void testRetag() {
  using laptags::LapInfo;
  std::vector<LapInfo> laps = {{90000, 3000, 0}, {88000, 3010, 0}};
  CHECK(laptags::retag(laps) == 1 && laps[0].tags == 0 && laps[1].tags == 0, "two clean laps");
  laps.push_back({85000, 2700, 0});  // 10% short: cut
  CHECK(laptags::retag(laps) == 1 && laps[2].tags == laptags::ODD_DISTANCE, "a short lap isn't the best");
  laps.push_back({94100, 3000, 0});  // 106.9%: not slow
  laps.push_back({94200, 3000, 0});  // 107.05%: slow
  laptags::retag(laps);
  CHECK(laps[3].tags == 0 && laps[4].tags == laptags::SLOW, "107%% of 88 s: %s / %s", tagText(laps[3].tags),
        tagText(laps[4].tags));
  laps[1].tags = laptags::PIT;  // the best was a pit lap after all: lap 1 is the best...
  CHECK(laptags::retag(laps) == 0, "best moves to lap 1");
  CHECK(laps[1].tags == laptags::PIT && laps[4].tags == 0, "...fix flags kept, and 94.2 s isn't slow against 90 s");
  std::vector<LapInfo> none = {{60000, 1000, laptags::GPS_GAP}};
  CHECK(laptags::retag(none) == -1, "no clean lap, no best");
  CHECK(laptags::fixFlags(4.9f, -1, 0) == laptags::PIT && laptags::fixFlags(5, -1, 0) == 0, "stop below 5 km/h");
  CHECK(laptags::fixFlags(50, 5.1f, 0) == laptags::POOR_ACCURACY && laptags::fixFlags(50, 5, 0) == 0, "accuracy over 5 m");
  CHECK(laptags::fixFlags(50, -1, 1001) == laptags::GPS_GAP && laptags::fixFlags(50, -1, 1000) == 0, "gap over 1 s");
  printf("  retag: distance off the median, 107%% slow, best skips tagged laps\n");
}

// A cut first lap is the reference until the median shows it up; then the
// next clean lap stands in, so the live delta carries on.
static void testReferenceDropped() {
  Driver d(14 * 3600000u);
  d.arc(0.0, 25);
  d.arc(M_PI / 2, 33);  // lap 1: cut, and quick
  d.chord(3 * M_PI / 2, 33);
  d.arc(TWO_PI + 0.05, 33);  // (the line is crossed by the first fix past it)
  CHECK(d.lt.bestLapIndex() == 0, "one lap: the cut lap is the best for now");
  d.arc(2 * TWO_PI + 0.05, 30);  // lap 2
  d.arc(3 * TWO_PI + 0.05, 31);  // lap 3: the median shows lap 1 up
  CHECK(d.lt.lapCount() == 3 && (d.lt.lap(0).tags & laptags::ODD_DISTANCE), "lap 1 suspect: %s",
        d.lt.lapCount() ? tagText(d.lt.lap(0).tags) : "-");
  CHECK(d.lt.bestLapIndex() == 2, "best is lap 3, got %d", d.lt.bestLapIndex() + 1);
  d.arc(3 * TWO_PI + M_PI, 31);
  CHECK(d.lt.liveDeltaMs() != NO_DELTA && std::abs(d.lt.liveDeltaMs()) < 300, "live delta against lap 3: %d",
        d.lt.liveDeltaMs());

  // The same, but lap 3 is slower than lap 2: lap 2 is the best, and lap 3's path stands in
  Driver e(15 * 3600000u);
  e.arc(0.0, 25);
  e.arc(M_PI / 2, 33);
  e.chord(3 * M_PI / 2, 33);
  e.arc(TWO_PI + 0.05, 33);
  e.arc(2 * TWO_PI + 0.05, 31);
  e.arc(3 * TWO_PI + 0.05, 30);
  CHECK(e.lt.bestLapIndex() == 1, "best is lap 2, got %d", e.lt.bestLapIndex() + 1);
  e.arc(3 * TWO_PI + M_PI, 30);
  CHECK(e.lt.liveDeltaMs() != NO_DELTA && std::abs(e.lt.liveDeltaMs()) < 300, "live delta against lap 3's path: %d",
        e.lt.liveDeltaMs());
  printf("  a cut lap stops being the reference once the median shows it up\n");
}

int main() {
  printf("Automatic sessions and lap tags\n");
  testRetag();
  testPitWatch();
  testSessionNumbers();
  testMorningSession();
  testReferenceDropped();
  if (failures) {
    printf("\n%d FAILURE(S)\n", failures);
    return 1;
  }
  printf("\nALL PASSED\n");
  return 0;
}
