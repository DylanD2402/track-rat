/**
 * Host-side tests for the on-device Adjust start / Adjust finish screen (include/AdjustLine.h):
 * which logs belong to the day, keeping only the fixes near a line while a log is read (the same
 * ones sliceNear() keeps, whatever the chunk size, and a long wait at the line adds almost nothing),
 * the day's runs numbered as the hub numbers them, and the screen's model: every time shown while the
 * line is nudged matches LapTimer timing the same fixes with the line there, and linenudge::newRunMs.
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_adjustline.cpp src/LapTimer.cpp src/VboFormat.cpp -o /tmp/adj && /tmp/adj
 */

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "AdjustLine.h"
#include "LapTimer.h"
#include "VboFormat.h"
#include "check.h"

using namespace adjust;

static const double REF_LAT = 45.0, REF_LON = -75.0;

static TrackPoint at(double x, double y, float heading = -1, float radius = 10) {
  TrackPoint p = {0, 0, radius, heading < 0 ? 0 : heading, heading < 0 ? HeadingFrom::None : HeadingFrom::Set};
  geo::fromLocal(x, y, REF_LAT, REF_LON, p.lat, p.lon);
  return p;
}

// A car feeding a LapTimer and recording every fix as a .vbo would, the lap column included
struct Car {
  LapTimer& timer;
  uint32_t ms = 36000000;
  double x = 0, y = 0;
  std::vector<vbo::Sample> log;
  std::vector<uint32_t> runs;  // the timer's run times, as they finish

  explicit Car(LapTimer& t) : timer(t) {}
  void fix(float kmh) {
    vbo::Sample s;
    geo::fromLocal(x, y, REF_LAT, REF_LON, s.lat, s.lon);
    for (const LapEvent& e : timer.onFix(s.lat, s.lon, kmh, ms, ms, -1))
      if (e.type == LapEventType::LapCompleted) runs.push_back(e.timeMs);
    s.utcMs = ms;
    s.speedKmh = kmh;
    s.sats = 12;
    s.hdop = 0.8f;
    s.lap = timer.lapRunning() ? timer.currentLapNumber() : 0;
    log.push_back(s);
    ms += 50;
  }
  void wait(double seconds) {
    for (int i = 0; i < seconds * 20; i++) fix(0);
  }
  void driveTo(double tx, double ty, float vmaxKmh, float startKmh = 0, double accel = 6) {
    double v = startKmh / 3.6;
    const double vmax = vmaxKmh / 3.6;
    const double len = hypot(tx - x, ty - y), ux = (tx - x) / len, uy = (ty - y) / len;
    double done = 0;
    while (done < len) {
      v = fmin(vmax, v + accel * 0.05);
      const double step = fmin(v * 0.05, len - done);
      done += step;
      x += ux * step;
      y += uy * step;
      fix((float)(v * 3.6));
    }
  }
  void stopWithin(double dx, double dy, float fromKmh) {
    double v = fromKmh / 3.6;
    const double len = hypot(dx, dy), ux = dx / len, uy = dy / len;
    const double decel = v * v / (2 * len);
    while (v > 0.05) {
      v = fmax(0, v - decel * 0.05);
      x += ux * v * 0.05;
      y += uy * v * 0.05;
      fix((float)(v * 3.6));
    }
    fix(0);
  }
};

// The course: start at the origin, finish 300 m east. A standing start, or a rolling one from the west.
static void driveRun(Car& c, float vmax, double lateral, bool standing, double waitS = 3) {
  if (standing) {
    c.wait(waitS);
    c.driveTo(150, lateral, vmax);
  } else {
    c.driveTo(150, lateral, vmax, 40, 8);
  }
  c.driveTo(300, 0, vmax, vmax);
  c.driveTo(330, 0, vmax, vmax);
  c.stopWithin(30, 0, vmax);
}
static void back(Car& c, double toX = 0) {
  c.driveTo(330, -40, 30, 0);
  c.driveTo(-30, -40, 30, 30);
  c.driveTo(-30, 0, 30, 30);
  c.driveTo(toX, 0, 20, 20);
  c.stopWithin(0.4, 0, 20);
}

static std::string logText(const std::vector<vbo::Sample>& samples, const TrackPoint& sf, const TrackPoint& fin, int session) {
  vbo::Header h;
  h.year = 2026;
  h.month = 10;
  h.day = 25;
  h.utcMs = samples.empty() ? 0 : samples[0].utcMs;
  h.comments = {"Track Rat GPS lap timer", "Started: 20261025_100000", "Type: stage", "Track: course-20261025",
                "Track name: Oct 25 course", "Session: " + std::to_string(session), "Start/finish: " + vbo::pointText(sf),
                "Finish: " + vbo::pointText(fin)};
  std::string text = vbo::headerText(h);
  for (const vbo::Sample& s : samples) {
    char buf[200];
    text.append(buf, vbo::formatLine(s, buf, sizeof(buf)));
  }
  return text;
}

// The same fixes timed by a fresh LapTimer with these lines
static std::vector<uint32_t> timeWith(const std::vector<vbo::Sample>& samples, const TrackPoint& sf, const TrackPoint& fin) {
  LapTimer t;
  t.begin(sf, {}, MIN_LAP_TIME_MS);
  t.setStage(fin, true);
  std::vector<uint32_t> out;
  for (const vbo::Sample& s : samples)
    for (const LapEvent& e : t.onFix(s.lat, s.lon, s.speedKmh, s.utcMs, s.utcMs, -1))
      if (e.type == LapEventType::LapCompleted) out.push_back(e.timeMs);
  return out;
}

static Setup setupFor(Which w, const TrackPoint& sf, const TrackPoint& fin) {
  Setup s;
  s.which = w;
  s.start = sf;
  s.hasFinish = true;
  s.finish = fin;
  return s;
}

static void scanLog(DayScan& day, const std::string& text, size_t chunk) {
  day.beginLog();
  for (size_t i = 0; i < text.size(); i += chunk) day.feed(text.data() + i, std::min(chunk, text.size() - i));
  day.endLog();
}

static Model modelFor(Which w, const TrackPoint& sf, const TrackPoint& fin, DayScan& day) {
  Model m;
  const TrackPoint& line = w == Which::Start ? sf : fin;
  m.setup(w, line, sf.lat, sf.lon, std::move(day.runs()));
  return m;
}

static void testLogNames() {
  const std::vector<std::string> names = {"20261025_101500_course-20261025.vbo", "20261025_101500_course-20261025.json",
                                          "20261025_094000_course-20261025.vbo", "20261025_094000_course-20261025_2.vbo",
                                          "20261024_094000_course-20261025.vbo", "20261025_110000_calabogie.vbo",
                                          "20261025_120000_course-20261025-2.vbo", "20261025_130000_course-20261025.csv"};
  const std::vector<std::string> got = logsForDay(names, "course-20261025", "20261025");
  CHECK(got.size() == 3, "three logs for the day: %d", (int)got.size());
  CHECK(got.size() == 3 && got[0] == "20261025_094000_course-20261025.vbo" && got[1] == "20261025_094000_course-20261025_2.vbo" &&
            got[2] == "20261025_101500_course-20261025.vbo",
        "in the order driven");
  CHECK(logsForDay({"/sessions/20261025_094000_course-20261025.vbo"}, "course-20261025", "20261025").size() == 1, "with a path");
}

static void testSlicerMatchesSliceNear() {
  std::vector<Fix> all;
  for (int i = 0; i < 900; i++) {
    Fix f;
    f.x = -150 + i * 0.4 + 6 * sin(i * 0.05);
    f.y = 30 * sin(i * 0.011);
    f.ms = 1000 + i * 50;
    f.kmh = 50;
    all.push_back(f);
  }
  for (double r : {20.0, 60.0}) {
    const std::vector<Fix> want = linenudge::sliceNear(all, 0, 0, r);
    Slicer s;
    s.begin(0, 0, r);
    for (const Fix& f : all) s.add(f);
    s.end();
    bool same = s.fixes().size() == want.size();
    for (size_t i = 0; same && i < want.size(); i++)
      same = s.fixes()[i].ms == want[i].ms && s.fixes()[i].joined == want[i].joined;
    CHECK(same, "the incremental slice is sliceNear's (radius %.0f): %d vs %d", r, (int)s.fixes().size(), (int)want.size());
  }
  // Two passes: the joins say where the gap is
  Slicer two;
  two.begin(0, 0, 10);
  for (int i = 0; i < 400; i++) {
    Fix f;
    f.x = fmod(i, 200) - 100;
    f.y = 0;
    f.ms = i * 100;
    f.kmh = 40;
    two.add(f);
  }
  two.end();
  int breaks = 0;
  for (const Fix& f : two.fixes()) breaks += !f.joined;
  CHECK(breaks == 2, "two passes, two starts to the slice: %d", breaks);
}

// A standing-start day: two sessions with two runs each, then every time in the model against LapTimer
static void testStandingDayFinish() {
  const TrackPoint sf = at(0, 0), fin = at(300.37, 0, 90);
  std::vector<std::string> logs;
  std::vector<std::vector<vbo::Sample>> samples;
  std::vector<uint32_t> truth;
  {  // session 1: runs 1 and 2
    LapTimer t;
    t.begin(sf, {}, MIN_LAP_TIME_MS);
    t.setStage(fin, true);
    Car c(t);
    c.wait(2);
    driveRun(c, 70, 0.5, true, 4);
    back(c);
    driveRun(c, 80, -1.0, true, 6);
    back(c);
    c.wait(3);
    CHECK(c.runs.size() == 2, "session 1 timed 2 runs: %d", (int)c.runs.size());
    truth.insert(truth.end(), c.runs.begin(), c.runs.end());
    samples.push_back(c.log);
    logs.push_back(logText(c.log, sf, fin, 1));
  }
  {  // session 2 (after a power-off): runs 3 and 4 (the second a stop short, not timed), then a fifth
    LapTimer t;
    t.begin(sf, {}, MIN_LAP_TIME_MS);
    t.setStage(fin, true);
    Car c(t);
    c.wait(1);
    driveRun(c, 75, 0.0, true, 3);
    back(c);
    // a run that stops in the middle and is never timed
    c.wait(2);
    c.driveTo(120, 0, 60);
    c.stopWithin(10, 0, 60);
    c.wait(7);
    c.driveTo(0, -40, 30, 0);
    c.driveTo(0, 0, 20, 20);
    c.stopWithin(0.3, 0, 20);
    c.wait(2);
    driveRun(c, 85, 1.0, true, 2);
    back(c);
    c.wait(2);
    CHECK(c.runs.size() == 2, "session 2 timed 2 runs: %d", (int)c.runs.size());
    truth.insert(truth.end(), c.runs.begin(), c.runs.end());
    samples.push_back(c.log);
    logs.push_back(logText(c.log, sf, fin, 2));
  }

  for (size_t chunk : {size_t(7), size_t(64), size_t(100000)}) {
    DayScan day;
    day.begin(setupFor(Which::Finish, sf, fin));
    for (const std::string& l : logs) scanLog(day, l, chunk);
    CHECK(day.runs().size() == 4, "four timed runs (chunk %d): %d", (int)chunk, (int)day.runs().size());
    CHECK(day.logs() == 2, "two logs");
    for (size_t i = 0; i < day.runs().size(); i++) CHECK(day.runs()[i].number == (int)i + 1, "run numbers go on across sessions");
    size_t most = 0;
    for (const RunData& r : day.runs()) most = std::max(most, r.near.size());
    CHECK(most < 400, "only the fixes near the line are kept: %d", (int)most);
    if (chunk != 64) continue;

    Model m = modelFor(Which::Finish, sf, fin, day);
    CHECK(m.size() == 4 && m.canMove(), "model has 4 runs");
    for (size_t i = 0; i < m.size() && i < truth.size(); i++)
      CHECK(llabs(m.oldMs(i) - (int64_t)truth[i]) <= 1, "run %d old time %lld matches LapTimer's %u", (int)i + 1, (long long)m.oldMs(i), truth[i]);

    // Nudge the finish: every time is what LapTimer gives with the finish there
    for (int steps : {1, 3, -2, -8, 20, -30}) {
      CHECK(m.reset() || true, "reset");
      m.nudge(steps);
      const double want = std::max(-LIMIT_M, std::min(LIMIT_M, steps * 0.5));
      CHECK(fabs(m.offsetM() - want) < 1e-9, "offset %.1f for %d steps", m.offsetM(), steps);
      TrackPoint moved = fin;
      linenudge::shiftPoint(moved, m.offsetM());
      std::vector<uint32_t> real;
      for (const auto& s : samples) {
        const std::vector<uint32_t> r = timeWith(s, sf, moved);
        real.insert(real.end(), r.begin(), r.end());
      }
      CHECK(real.size() == 4, "LapTimer times 4 runs with the finish at %+.1f m", m.offsetM());
      for (size_t i = 0; i < m.size() && i < real.size(); i++)
        CHECK(llabs(m.newMs(i) - (int64_t)real[i]) <= 1, "finish %+.1f m run %d: %lld vs LapTimer %u", m.offsetM(), (int)i + 1,
              (long long)m.newMs(i), real[i]);
    }
    // 0.5 m at about 80 km/h is about 22 ms; later finish, longer run
    m.reset();
    m.nudge(1);
    for (size_t i = 0; i < m.size(); i++) {
      const int64_t d = m.newMs(i) - m.oldMs(i);
      CHECK(d >= 15 && d <= 40, "0.5 m later adds 15-40 ms, run %d: %lld", (int)i + 1, (long long)d);
    }
    // The view
    View v;
    m.fillView(v);
    CHECK(v.rows.size() == 4 && v.paths.size() == 4, "view has a row and a path per run");
    CHECK(v.offsetText == "Moved +0.5 m (later)", "offset text: %s", v.offsetText.c_str());
    CHECK(v.rows[0].number == 1 && v.rows[0].oldText == timeText(m.oldMs(0)) && v.rows[0].newText == timeText(m.newMs(0)),
          "row texts are the model's times");
    int bests = 0;
    for (const ViewRow& r : v.rows) bests += r.best;
    CHECK(bests == 1, "one best");
    CHECK(v.rows[0].changed && !v.rows[0].diffText.empty(), "diff text %s", v.rows[0].diffText.c_str());
    CHECK(!v.paths[0].empty(), "path points for the plot");
    // newRunMs agrees (the time is the crossing from the start)
    const RunData& r0 = m.run(0);
    Line l = m.line();
    linenudge::shift(l, 0.5, m.directionDeg());
    const int64_t fm = linenudge::crossingTimeMs(r0.near, l, 10, r0.moveOffMs);
    CHECK(linenudge::newRunMs(NONE, fm, true, r0.moveOffMs) == m.newMs(0), "the same as newRunMs");
    // Reset and Save's point
    CHECK(m.reset() && m.offsetM() == 0 && m.newMs(0) == m.oldMs(0), "reset puts the times back");
    CHECK(!m.reset(), "reset when not moved changes nothing");
    m.nudge(5);
    const TrackPoint saved = m.moved();
    TrackPoint expect = fin;
    linenudge::shiftPoint(expect, 2.5);
    CHECK(fabs(saved.lat - expect.lat) < 1e-9 && fabs(saved.lon - expect.lon) < 1e-9 && saved.heading == fin.heading, "Save writes the line 2.5 m on");
    CHECK(geo::distanceM(saved.lat, saved.lon, fin.lat, fin.lon) > 2.4 && geo::distanceM(saved.lat, saved.lon, fin.lat, fin.lon) < 2.6,
          "2.5 m away");
  }
}

// A standing start with the start line at the car, and then a rolling start: Adjust start
static void testAdjustStart() {
  // Standing starts with a start line facing east (so a line moved ahead of the car starts the clock at the line)
  const TrackPoint sf = at(0, 0, 90), fin = at(300.37, 0, 90);
  LapTimer t;
  t.begin(sf, {}, MIN_LAP_TIME_MS);
  t.setStage(fin, true);
  Car c(t);
  c.wait(2);
  driveRun(c, 70, 0.0, true, 4);
  back(c, -80);
  driveRun(c, 80, 0.5, false);  // a rolling start from the west
  back(c, -0.5);
  c.wait(2);
  CHECK(c.runs.size() == 2, "two runs: %d", (int)c.runs.size());

  DayScan day;
  day.begin(setupFor(Which::Start, sf, fin));
  scanLog(day, logText(c.log, sf, fin, 1), 256);
  CHECK(day.runs().size() == 2, "two runs read: %d", (int)day.runs().size());
  Model m = modelFor(Which::Start, sf, fin, day);
  CHECK(m.canMove(), "can move");
  for (size_t i = 0; i < 2; i++) CHECK(llabs(m.oldMs(i) - (int64_t)c.runs[i]) <= 1, "old %d: %lld vs %u", (int)i, (long long)m.oldMs(i), c.runs[i]);

  View v;
  m.fillView(v);
  CHECK(!v.note.empty(), "a note about standing starts");
  CHECK(v.note.find("1 m ahead") != std::string::npos, "the note says 1 m: %s", v.note.c_str());

  for (int steps : {1, 4, -6, 20, -30}) {
    m.reset();
    m.nudge(steps);
    TrackPoint moved = sf;
    linenudge::shiftPoint(moved, m.offsetM());
    const std::vector<uint32_t> real = timeWith(c.log, moved, fin);
    CHECK(real.size() == 2, "LapTimer still times 2 runs with the start at %+.1f m: %d", m.offsetM(), (int)real.size());
    for (size_t i = 0; i < real.size() && i < 2; i++)
      CHECK(llabs(m.newMs(i) - (int64_t)real[i]) <= 1, "start %+.1f m run %d: %lld vs LapTimer %u", m.offsetM(), (int)i + 1, (long long)m.newMs(i), real[i]);
    if (m.offsetM() > 1.0) CHECK(m.newMs(0) < m.oldMs(0), "a standing run starts at a line more than 1 m ahead: shorter");
    if (m.offsetM() <= 1.0 && m.offsetM() >= -5) CHECK(m.newMs(0) == m.oldMs(0), "a standing start is timed from moving off at %+.1f m", m.offsetM());
    if (m.offsetM() > 0) CHECK(m.newMs(1) < m.oldMs(1), "a rolling start is shorter with the line later");
  }

  // A start with no direction: only standing starts, however it is nudged within the car's reach
  const TrackPoint sf0 = at(0, 0);
  LapTimer t0;
  t0.begin(sf0, {}, MIN_LAP_TIME_MS);
  t0.setStage(fin, true);
  Car c0(t0);
  c0.wait(2);
  driveRun(c0, 70, 0.0, true, 4);
  back(c0);
  c0.wait(2);
  DayScan day0;
  day0.begin(setupFor(Which::Start, sf0, fin));
  scanLog(day0, logText(c0.log, sf0, fin, 1), 4096);
  CHECK(day0.runs().size() == 1, "one run: %d", (int)day0.runs().size());
  Model m0 = modelFor(Which::Start, sf0, fin, day0);
  CHECK(m0.canMove(), "the runs' direction stands in for the start's");
  m0.nudge(10);
  CHECK(m0.newMs(0) == m0.oldMs(0), "5 m with no direction: a standing start is timed from moving off");
  const TrackPoint saved = m0.moved();
  CHECK(!saved.hasHeading(), "the line stays without a direction");
  CHECK(geo::distanceM(saved.lat, saved.lon, sf0.lat, sf0.lon) > 4.9, "but it moved");
  View v0;
  m0.fillView(v0);
  CHECK(v0.note.find("25 m") != std::string::npos, "the no-direction note says 25 m: %s", v0.note.c_str());
}

static void testWaitingAtTheLineIsCheap() {
  const TrackPoint sf = at(0, 0), fin = at(300.37, 0, 90);
  LapTimer t;
  t.begin(sf, {}, MIN_LAP_TIME_MS);
  t.setStage(fin, true);
  Car c(t);
  c.wait(300);  // five minutes in the grid: 6000 fixes
  driveRun(c, 70, 0.0, true, 5);
  c.wait(30);
  CHECK(c.runs.size() == 1, "timed");
  DayScan day;
  day.begin(setupFor(Which::Finish, sf, fin));
  scanLog(day, logText(c.log, sf, fin, 1), 512);
  CHECK(day.runs().size() == 1, "one run");
  CHECK(!day.truncated(), "not cut short");
  Model m = modelFor(Which::Finish, sf, fin, day);
  CHECK(llabs(m.oldMs(0) - (int64_t)c.runs[0]) <= 1, "standing start after a long wait: %lld vs %u", (long long)m.oldMs(0), c.runs[0]);
}

static void testCapAndMissing() {
  const TrackPoint sf = at(0, 0), fin = at(300.37, 0, 90);
  LapTimer t;
  t.begin(sf, {}, MIN_LAP_TIME_MS);
  t.setStage(fin, true);
  Car c(t);
  c.wait(2);
  for (int i = 0; i < 24; i++) {
    driveRun(c, 70 + i % 5, 0.0, true, 3);
    back(c);
  }
  CHECK(c.runs.size() == 24, "24 runs: %d", (int)c.runs.size());
  DayScan day;
  day.begin(setupFor(Which::Finish, sf, fin));
  scanLog(day, logText(c.log, sf, fin, 1), 1000);
  CHECK(day.runs().size() == MAX_RUNS, "the latest %d are kept: %d", (int)MAX_RUNS, (int)day.runs().size());
  CHECK(day.counted() == 24, "but all 24 were counted");
  CHECK(day.runs().front().number == 5 && day.runs().back().number == 24, "numbers 5 to 24: %d..%d", day.runs().front().number, day.runs().back().number);

  // No finish: nothing is timed
  Setup none = setupFor(Which::Finish, sf, fin);
  none.hasFinish = false;
  DayScan nofin;
  nofin.begin(none);
  scanLog(nofin, logText(c.log, sf, fin, 1), 1000);
  CHECK(nofin.runs().empty(), "no finish, no runs");

  // An empty or damaged log
  DayScan empty;
  empty.begin(setupFor(Which::Finish, sf, fin));
  scanLog(empty, "", 10);
  scanLog(empty, "garbage\r\nmore garbage\r\n", 5);
  CHECK(empty.runs().empty(), "garbage gives no runs");
  Model em;
  em.setup(Which::Finish, fin, sf.lat, sf.lon, std::move(empty.runs()));
  View v;
  em.fillView(v);
  CHECK(v.rows.empty() && v.summary.empty(), "an empty model draws nothing");
}

static void testBlockAndText() {
  CHECK(block(Which::Finish, true, true, 3, false, true, false) == Block::None, "enabled");
  CHECK(block(Which::Finish, true, false, 3, false, true, false) == Block::NoFinish, "no finish");
  CHECK(block(Which::Start, true, false, 0, false, true, false) == Block::NoFinish, "start without a finish: nothing is timed");
  CHECK(block(Which::Start, false, true, 3, false, true, false) == Block::NoStart, "no start");
  CHECK(block(Which::Finish, true, true, 0, false, true, false) == Block::NoRuns, "no runs");
  CHECK(block(Which::Finish, true, true, 0, true, true, false) == Block::Reading, "still reading");
  CHECK(block(Which::Finish, true, true, 3, false, false, false) == Block::NoCard, "no card");
  CHECK(block(Which::Finish, true, true, 3, false, true, true) == Block::Retiming, "re-timing");
  CHECK(std::string(blockText(Block::NoFinish)).size() <= 16, "short enough for the button");
  CHECK(std::string(blockText(Block::None)).empty(), "no reason when enabled");

  CHECK(timeText(53400) == "53.40" && timeText(62310) == "1:02.31" && timeText(NONE) == "-", "time text");
  CHECK(diffText(53400, 53460) == "+0.06" && diffText(53400, 53280) == "-0.12" && diffText(53400, 53403) == "" && diffText(NONE, 5) == "", "diff text");
  CHECK(diffText(50000, 61500) == "+11.50", "a long diff: %s", diffText(50000, 61500).c_str());
  CHECK(offsetText(0) == "Not moved" && offsetText(1.5) == "Moved +1.5 m (later)" && offsetText(-0.5) == "Moved -0.5 m (earlier)", "offset text");
  CHECK(llabs(0) == 0, "");
}

int main() {
  printf("Adjust line tests\n");
  testLogNames();
  testSlicerMatchesSliceNear();
  testStandingDayFinish();
  testAdjustStart();
  testWaitingAtTheLineIsCheap();
  testCapAndMissing();
  testBlockAndText();
  if (failures) {
    printf("%d failed\n", failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
