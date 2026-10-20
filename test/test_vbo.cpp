/**
 * Host-side tests for the VBO session log format.
 *
 * Format, g-forces, timing lines, boot recovery (SessionReplay) and the log
 * writer's buffer (LogRing), with synthetic sessions in every hemisphere.
 *
 *   g++ -std=c++17 -pthread -DDEBUG_MODE=0 -Iinclude test/test_vbo.cpp src/VboFormat.cpp src/SessionReplay.cpp src/LapTimer.cpp -o /tmp/vbo && /tmp/vbo
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "LapTimer.h"
#include "LogRing.h"
#include "SessionReplay.h"
#include "VboFormat.h"
#include "check.h"

static std::string line(const vbo::Sample& s) {
  char buf[160];
  size_t n = vbo::formatLine(s, buf, sizeof(buf));
  return std::string(buf, n);
}

static std::vector<std::string> tokens(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  for (std::string t; in >> t;) out.push_back(t);
  return out;
}

// Splits on CRLF; fails the test on any bare LF or CR.
static std::vector<std::string> crlfLines(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '\n') {
      CHECK(i > 0 && text[i - 1] == '\r', "bare LF at byte %zu", i);
      out.push_back(text.substr(start, i - start - 1));
      start = i + 1;
    } else if (text[i] == '\r') {
      CHECK(i + 1 < text.size() && text[i + 1] == '\n', "bare CR at byte %zu", i);
    }
  }
  CHECK(start == text.size(), "text doesn't end with CRLF");
  return out;
}

static void testCoordinates() {
  vbo::Sample s;
  s.lat = 45.123456;   // north: + minutes
  s.lon = -76.123456;  // west: + minutes (reversed)
  std::vector<std::string> t = tokens(line(s));
  CHECK(t[2] == "002707.40736", "45.123456 N -> %s", t[2].c_str());
  CHECK(t[3] == "004567.40736", "76.123456 W -> %s", t[3].c_str());

  s.lat = -33.5;      // south: - minutes
  s.lon = 6.947222;   // east: - minutes
  t = tokens(line(s));
  CHECK(t[2] == "-02010.00000", "33.5 S -> %s", t[2].c_str());
  CHECK(t[3] == "-00416.83332", "6.947222 E -> %s", t[3].c_str());

  // A line from Racelogic's own example file (Texas World Speedway), plus our channels
  s = vbo::Sample();
  s.sats = 7;
  s.utcMs = 14 * 3600000 + 2 * 60000 + 8800;
  s.lat = 1832.29573 / 60.0;
  s.lon = -5773.42012 / 60.0;
  s.speedKmh = 30.725f;
  s.headingDeg = 223.24f;
  s.heightM = 43.70f;
  s.hdop = 0.8f;
  s.lap = 3;
  s.latG = 0.123f;
  s.longG = -0.456f;
  s.tag = 5;
  const std::string want = "007 140208.800 001832.29573 005773.42012 030.725 223.24 +00043.70 00.80 003 +0.123 -0.456 005\r\n";
  CHECK(line(s) == want, "line:\n  got  %s  want %s", line(s).c_str(), want.c_str());
  printf("  coordinates: N/S/E/W signs, minutes x 100000 resolution, sample line matches\n");
}

static void testTime() {
  char t[11];
  struct { uint32_t ms; const char* want; } cases[] = {
    {0, "000000.000"},           {45296789, "123456.789"}, {86399950, "235959.950"},
    {86400000, "000000.000"},    // one day on: wraps like the GPS clock
    {50528800, "140208.800"},
  };
  for (auto& c : cases) {
    vbo::formatTime(c.ms, t);
    CHECK(strcmp(t, c.want) == 0, "%u ms -> %s, want %s", c.ms, t, c.want);
  }
  CHECK(vbo::utcDiffMs(86399950, 0) == 50, "diff across midnight %d", vbo::utcDiffMs(86399950, 0));
  CHECK(vbo::utcDiffMs(0, 86399950) == -50, "diff back across midnight %d", vbo::utcDiffMs(0, 86399950));
  CHECK(vbo::utcDiffMs(1000, 1050) == 50, "plain diff");

  // Read back, including across midnight
  vbo::Reader r;
  vbo::Sample s;
  r.feed("[header]", s);
  for (const char* c : {"satellites", "time", "latitude", "longitude"}) r.feed(c, s);
  r.feed("[data]", s);
  CHECK(r.feed("007 235959.950 002707.40736 004567.40736", s) && s.utcMs == 86399950, "read 23:59:59.95 -> %u", s.utcMs);
  CHECK(r.feed("007 000000.000 002707.40736 004567.40736\r", s) && s.utcMs == 0, "read 00:00:00.00 -> %u", s.utcMs);
  CHECK(r.feed("007 140208.80 002707.40736 004567.40736", s) && s.utcMs == 50528800, "2 decimals -> %u", s.utcMs);
  CHECK(fabs(s.lat - 45.123456) < 1e-9 && fabs(s.lon + 76.123456) < 1e-9, "read back %.9f %.9f", s.lat, s.lon);
  CHECK(!r.feed("007 140208.80 002707.4", s), "a line cut short is rejected");
  CHECK(!r.feed("007 250000.00 002707.40736 004567.40736", s), "hour 25 is rejected");
  CHECK(r.badLines() == 2, "bad lines %u", r.badLines());
  printf("  time: hhmmss.sss, wraps at UTC midnight, read back with 2 or 3 decimals\n");
}

static void testGForce() {
  // Speeding up 1 m/s every 50 ms (2.04 g) at 20 Hz, through UTC midnight
  vbo::GForce g;
  uint32_t t = 86400000 - 1000;
  float v = 10.0f;
  for (int i = 0; i < 40; i++) {
    g.update(t, v * 3.6f, 0, true);
    t = (t + 50) % 86400000;
    v += 1.0f;
  }
  CHECK(fabs(g.longitudinal() - 1.0f / 0.05f / 9.80665f) < 0.01f, "longitudinal %.3f g", g.longitudinal());
  CHECK(fabs(g.lateral()) < 0.001f, "straight line lateral %.3f g", g.lateral());
  printf("  accelerating through midnight: %.3f g (expected %.3f)\n", g.longitudinal(), 1.0f / 0.05f / 9.80665f);

  // Right-hand circle, 20 m/s on 50 m radius: v^2/r = 0.816 g, heading wrapping past 360
  g.reset();
  double heading = 300;
  for (int i = 0; i < 60; i++) {
    g.update(1000 + 50 * i, 72.0f, (float)fmod(heading, 360.0), true);
    heading += 20.0 / 50.0 * 0.05 * 180.0 / M_PI;
  }
  const float want = 20.0f * 20.0f / 50.0f / 9.80665f;
  CHECK(fabs(g.lateral() - want) < 0.01f, "lateral %.3f g, want %.3f", g.lateral(), want);
  CHECK(fabs(g.longitudinal()) < 0.001f, "constant speed longitudinal %.3f", g.longitudinal());
  printf("  right-hand turn: %+.3f g (expected %+.3f)\n", g.lateral(), want);

  // Heading not valid (slow): no lateral g; a gap starts over
  g.update(1000 + 50 * 60, 72.0f, 10.0f, false);
  g.update(1000 + 50 * 61, 72.0f, 50.0f, false);
  CHECK(fabs(g.lateral()) < want * 0.7f, "lateral should decay without a heading: %.3f", g.lateral());
  g.update(1000 + 50 * 61 + 5000, 10.0f, 50.0f, true);
  CHECK(g.lateral() == 0 && g.longitudinal() == 0, "5 s gap should reset");
}

static void testGate() {
  const double refLat = 45.0, refLon = -76.0;
  // A straight north-south line through the start/finish
  std::vector<std::vector<geo::XYf>> map(1);
  for (int y = -100; y <= 100; y += 5) map[0].push_back({0.3f, (float)y});
  vbo::Gate g;
  CHECK(vbo::gateFromMap(map, refLat, refLon, {refLat, refLon, 15}, g), "gate on a straight");
  geo::XY a = geo::toLocal(g.lat1, g.lon1, refLat, refLon), b = geo::toLocal(g.lat2, g.lon2, refLat, refLon);
  CHECK(fabs(a.y) < 0.01 && fabs(b.y) < 0.01, "gate should run east-west: y %.3f %.3f", a.y, b.y);
  CHECK(fabs(fabs(a.x - b.x) - 30.0) < 0.01, "gate width %.3f", fabs(a.x - b.x));
  CHECK(fabs(a.x + b.x) < 0.01, "gate centred on the point");
  CHECK(!vbo::gateFromMap(map, refLat, refLon, {refLat, refLon + 50 / 78710.0, 15}, g), "50 m off the map: no gate");
  CHECK(!vbo::gateFromMap({}, refLat, refLon, {refLat, refLon, 15}, g), "no map: no gate");

  // The Bench Test circle: a closed loop starting and ending at the line
  std::vector<std::vector<geo::XYf>> circle(1);
  for (int i = 0; i <= 72; i++) {
    double t = i * 2 * M_PI / 72;
    circle[0].push_back({(float)(60 * (cos(t) - 1)), (float)(60 * sin(t))});
  }
  CHECK(vbo::gateFromMap(circle, refLat, refLon, {refLat, refLon, 15}, g), "gate on the circle");
  a = geo::toLocal(g.lat1, g.lon1, refLat, refLon);
  b = geo::toLocal(g.lat2, g.lon2, refLat, refLon);
  CHECK(fabs(a.y - b.y) < 0.05, "circle gate should be square to the track: dy %.3f", a.y - b.y);
  printf("  timing line from the map: %.1f m wide, square to the track (straight and closed loop)\n",
         fabs(a.x - b.x));
}

// --- whole sessions ------------------------------------------------------------

// A synthetic lap session: 5 laps of a 150 m radius circle at 20 Hz (a
// different speed each lap), timed live as on the device, logged as a VBO.
struct SessionFile {
  std::string text;          // the whole .vbo
  size_t headerLen = 0, lineLen = 0;
  int fixes = 0;
  bool sameLength = true;
  LapTimer live;
  TrackPoint sf;
  std::vector<TrackPoint> splits;
  vbo::Gate start;
  std::vector<int> lapsDoneAfter;  // laps completed after each fix
};

static double metresPerDegLon(double lat) { return 111194.93 * cos(lat * geo::DEG2RAD); }

// heading/from: the start/finish line's direction, if it has one. Stops after 5 laps or maxFixes.
static void makeSession(SessionFile& s, double sfLat, double sfLon, uint32_t startUtc, bool lineInHeader = true,
                        float heading = 0, HeadingFrom from = HeadingFrom::None, int maxFixes = 100000) {
  const double R = 150.0;
  auto circlePos = [&](double angle, double& lat, double& lon) {
    lat = sfLat + R * sin(angle) / 111194.93;
    lon = sfLon + R * (cos(angle) - 1.0) / metresPerDegLon(sfLat);
  };
  std::vector<std::vector<geo::XYf>> map(1);
  for (int i = 0; i <= 72; i++) {
    double t = i * 2 * M_PI / 72;
    map[0].push_back({(float)(R * (cos(t) - 1)), (float)(R * sin(t))});
  }
  s.sf = {sfLat, sfLon, 15, heading, from};
  s.splits = {{sfLat, sfLon - 2 * R / metresPerDegLon(sfLat), 15}};

  vbo::Header h;
  h.year = 2026;
  h.month = 10;
  h.day = 18;
  h.utcMs = startUtc;
  h.comments = {"Track Rat GPS lap timer", "Started: 20261018_235800", "Type: laps", "Track: test",
                "Track name: Test Circle"};
  if (lineInHeader) {  // as SessionLog::beginTrackLog writes them
    h.comments.push_back("Start/finish: " + vbo::pointText(s.sf));
    h.comments.push_back("Split 1: " + vbo::pointText(s.splits[0]));
    CHECK(vbo::gateFromMap(map, sfLat, sfLon, s.sf, h.start), "start gate");
    h.hasStart = true;
    vbo::Gate sg;
    CHECK(vbo::gateFromMap(map, sfLat, sfLon, s.splits[0], sg), "split gate");
    h.splits.push_back(sg);
    s.start = h.start;
  }
  h.comments.push_back("Min lap ms: 20000");
  s.text = vbo::headerText(h);
  s.headerLen = s.text.size();

  s.live.begin(s.sf, s.splits);
  vbo::GForce g;
  double angle = -0.2;
  for (int i = 0; s.live.lapCount() < 5 && i < maxFixes; i++) {
    const double v = 30.0 + 3.0 * s.live.lapCount();
    double lat, lon;
    circlePos(angle, lat, lon);
    uint32_t t = (startUtc + i * 50) % 86400000;
    s.live.onFix(lat, lon, (float)(v * 3.6), t, i * 50);
    float heading = (float)fmod(360.0 - angle * 180.0 / M_PI, 360.0);  // anticlockwise
    g.update(t, (float)(v * 3.6), heading, true);
    vbo::Sample smp;
    smp.utcMs = t;
    smp.lat = lat;
    smp.lon = lon;
    smp.speedKmh = (float)(v * 3.6);
    smp.headingDeg = heading;
    smp.heightM = -12.5f;
    smp.sats = 14;
    smp.hdop = 0.6f;
    smp.lap = s.live.lapRunning() ? s.live.currentLapNumber() : 0;
    smp.latG = g.lateral();
    smp.longG = g.longitudinal();
    std::string l = line(smp);
    if (s.lineLen == 0) s.lineLen = l.size();
    s.sameLength &= l.size() == s.lineLen;
    s.text += l;
    s.fixes++;
    s.lapsDoneAfter.push_back((int)s.live.lapCount());
    angle += v * 0.05 / R;
  }
}

// Layout: sections in order, CRLF only, channel/unit/column counts agree.
static void checkLayout(const SessionFile& s, const char* where) {
  CHECK(s.sameLength, "%s: every data line should be the same length (%zu)", where, s.lineLen);
  CHECK(s.text.size() == s.headerLen + s.fixes * s.lineLen, "%s: file size %zu", where, s.text.size());
  std::vector<std::string> lines = crlfLines(s.text);
  CHECK(lines[0] == "File created on 18/10/2026 @ 23:58:00", "first line: %s", lines[0].c_str());
  CHECK(lines[1].empty(), "blank line after the date");
  std::vector<std::string> order;
  size_t channels = 0, units = 0, columns = 0, gates = 0;
  std::string section;
  for (size_t i = 1; i < lines.size(); i++) {
    const std::string& l = lines[i];
    if (!l.empty() && l[0] == '[') {
      CHECK(lines[i - 1].empty(), "no blank line before %s", l.c_str());
      section = l;
      order.push_back(l);
      continue;
    }
    if (l.empty()) continue;
    if (section == "[header]") channels++;
    else if (section == "[channel units]") units++;
    else if (section == "[column names]") columns = tokens(l).size();
    else if (section == "[laptiming]") {
      gates++;
      std::vector<std::string> t = tokens(l);
      CHECK(t.size() == 7 && (t[0] == "Start" || t[0] == "Split") && t[5] == "\xC2\xAC", "gate line: %s", l.c_str());
      if (t[0] == "Start") {  // longitude first, sign reversed, in minutes
        CHECK(fabs(atof(t[1].c_str()) - vbo::lonToVbo(s.start.lon1)) < 1e-5 &&
                  fabs(atof(t[2].c_str()) - vbo::latToVbo(s.start.lat1)) < 1e-5,
              "%s: start gate coordinates: %s", where, l.c_str());
        CHECK(t[6] == "StartFinish", "start gate name %s", t[6].c_str());
      }
    } else if (section == "[data]") {
      CHECK(tokens(l).size() == channels, "data line has %zu columns, %zu channels", tokens(l).size(), channels);
    }
  }
  const std::vector<std::string> wantOrder = {"[header]", "[channel units]", "[comments]", "[laptiming]",
                                              "[column names]", "[data]"};
  CHECK(order == wantOrder, "%s: sections out of order", where);
  CHECK(channels == 12, "%zu channels", channels);
  CHECK(units == channels - 7, "%zu unit lines for %zu non-standard channels", units, channels - 7);
  CHECK(columns == channels, "%zu column names for %zu channels", columns, channels);
  CHECK(gates == 2, "%zu timing lines", gates);
}

// Replays `text` in chunks of 1..maxChunk bytes.
static void replay(SessionReplay& r, const std::string& text, size_t maxChunk, unsigned seed = 1) {
  testSeed(seed);
  for (size_t i = 0; i < text.size();) {
    size_t n = 1 + testRand() % maxChunk;
    if (n > text.size() - i) n = text.size() - i;
    r.feed(text.data() + i, n);
    i += n;
  }
  r.finish();
}

// tolMs: positions in the log are rounded to about 2 cm, which can move a
// crossing by half a millisecond at 150 km/h.
static void checkSameLaps(const LapTimer& got, const LapTimer& want, size_t laps, const char* where, int tolMs = 1) {
  CHECK(got.lapCount() == laps, "%s: re-timed %zu laps, want %zu", where, got.lapCount(), laps);
  for (size_t i = 0; i < got.lapCount() && i < laps; i++) {
    int32_t d = (int32_t)got.lap(i).timeMs - (int32_t)want.lap(i).timeMs;
    CHECK(abs(d) <= tolMs, "%s: lap %zu: re-timed %u ms, live %u ms", where, i + 1, got.lap(i).timeMs,
          want.lap(i).timeMs);
    CHECK(got.lap(i).splitMs.size() == 1 && abs(got.lap(i).splitMs[0] - want.lap(i).splitMs[0]) <= tolMs,
          "%s: lap %zu split", where, i + 1);
  }
}

// All four hemispheres, and a session that crosses the Greenwich meridian
// (longitude changes sign mid-lap).
static void testHemispheres() {
  struct Place { const char* name; double lat, lon; } places[] = {
    {"California (N, W)", 37.0, -122.0},
    {"Sydney (S, E)", -33.8688, 151.2093},
    {"Buenos Aires (S, W)", -34.6037, -58.3816},
    {"Spa (N, E)", 50.4372, 5.9714},
    {"Greenwich (lon 0)", 51.4769, 0.0010},
  };
  for (const Place& p : places) {
    SessionFile s;
    makeSession(s, p.lat, p.lon, 86400000 - 120000);  // starts two minutes before UTC midnight
    checkLayout(s, p.name);
    SessionReplay r;
    replay(r, s.text, 4096);
    CHECK(r.samples == (uint32_t)s.fixes, "%s: read %u of %d fixes", p.name, r.samples, s.fixes);
    CHECK(r.reader().badLines() == 0 && !r.cutLine, "%s: %u bad lines", p.name, r.reader().badLines());
    checkSameLaps(r.timer, s.live, 5, p.name);
    printf("  %-20s %d fixes, %zu-byte lines, 5 laps re-timed to within 1 ms\n", p.name, s.fixes, s.lineLen);
  }
}

// What boot recovery sees after a power cut.
static void testRecovery() {
  SessionFile s;
  makeSession(s, 37.0, -122.0, 86400000 - 120000);

  // Whole file in odd chunk sizes (lines split across reads)
  {
    SessionReplay r;
    replay(r, s.text, 7);
    CHECK(r.samples == (uint32_t)s.fixes && r.timed && !r.freeRoam, "small chunks: %u samples", r.samples);
    CHECK(r.trackId == "test" && r.trackName == "Test Circle" && r.stamp == "20261018_235800", "header fields");
    checkSameLaps(r.timer, s.live, 5, "small chunks");
    // 5 laps and the out lap, across midnight: the log covers this long
    uint32_t want = (uint32_t)(s.fixes - 1) * 50;
    CHECK(r.durationMs == want, "duration %u, want %u", r.durationMs, want);
  }

  // Power cut mid-line, 70% through the session
  const int k = s.fixes * 7 / 10;
  for (size_t cutAt : {(size_t)17, s.lineLen - 3, s.lineLen - 1}) {  // mid-line, inside the last number, before LF
    SessionReplay r;
    replay(r, s.text.substr(0, s.headerLen + k * s.lineLen + cutAt), 4096);
    CHECK(r.samples == (uint32_t)k, "cut %zu bytes into a line: %u samples, want %d", cutAt, r.samples, k);
    CHECK(r.cutLine, "cut line should be reported");
    CHECK(r.reader().badLines() == 0, "cut line counted as bad: %u", r.reader().badLines());
    checkSameLaps(r.timer, s.live, s.lapsDoneAfter[k - 1], "after a cut");
  }
  {  // cut exactly at the end of a line: nothing lost, nothing reported
    SessionReplay r;
    replay(r, s.text.substr(0, s.headerLen + k * s.lineLen), 4096);
    CHECK(r.samples == (uint32_t)k && !r.cutLine, "clean cut: %u samples", r.samples);
  }
  {  // FAT can leave zeros after the last write: they're not data
    SessionReplay r;
    std::string t = s.text.substr(0, s.headerLen + k * s.lineLen) + std::string(1000, '\0') + "\r\n";
    replay(r, t, 4096);
    CHECK(r.samples == (uint32_t)k, "trailing zeros: %u samples", r.samples);
  }
  {  // only the header made it to the card
    SessionReplay r;
    replay(r, s.text.substr(0, s.headerLen), 4096);
    CHECK(r.samples == 0 && r.trackId == "test" && r.timer.lapCount() == 0, "header only");
  }
  {  // a line of garbage in the middle is skipped, the rest still counts
    SessionReplay r;
    std::string t = s.text;
    t.insert(s.headerLen + 100 * s.lineLen, "007 12x456.000 garbage\r\n");
    replay(r, t, 4096);
    CHECK(r.samples == (uint32_t)s.fixes && r.reader().badLines() == 1, "garbage line: %u samples, %u bad",
          r.samples, r.reader().badLines());
  }

  // The line was marked during the session, so the header has none: the
  // track's current line (from tracks.json) is used, or there's no timing.
  SessionFile marked;
  makeSession(marked, 37.0, -122.0, 3600000, false);
  {
    SessionReplay r([&](const std::string& id, TrackPoint& sf, std::vector<TrackPoint>& splits) {
      if (id != "test") return false;
      sf = marked.sf;
      splits = marked.splits;
      return true;
    });
    replay(r, marked.text, 4096);
    CHECK(r.timed, "line from the track list");
    checkSameLaps(r.timer, marked.live, 5, "line from the track list");
  }
  {
    SessionReplay r;
    replay(r, marked.text, 4096);
    CHECK(!r.timed && r.timer.lapCount() == 0 && r.samples == (uint32_t)marked.fixes, "no line anywhere: untimed");
  }
  printf("  recovery: cut mid-line / in the last number / before LF dropped, trailing zeros ignored, line from track list\n");
}

// A session switched mid-session to another layout at the same line (laps
// kept) has a log whose header names the first layout and a summary naming
// the second. Recovery times it with the second's lines, if they share the
// header's start/finish; any other summary is ignored.
static void testSwitchedLayoutRecovery() {
  const double lat = 37.0, lon = -122.0, R = 150.0;  // makeSession's circle
  SessionFile s, unmarked;
  makeSession(s, lat, lon, 86400000 - 120000, true, 0, HeadingFrom::Set);  // as "test", split half way round
  makeSession(unmarked, lat, lon, 86400000 - 120000, false, 0, HeadingFrom::Set);  // the same, line marked live
  TrackPoint quarter = {lat + R / 111194.93, lon - R / metresPerDegLon(lat), 15};
  TrackPoint farSf = s.sf;  // another line, 50 m away
  farSf.lon += 50 / metresPerDegLon(lat);
  TrackPoint reverseSf = s.sf;
  reverseSf.heading = 180;
  std::map<std::string, std::pair<TrackPoint, std::vector<TrackPoint>>> tracks = {
    {"test", {s.sf, s.splits}},
    {"test.short", {s.sf, {quarter}}},  // the same line, a split a quarter of the way round
    {"test.far", {farSf, {quarter}}},
    {"test.reverse", {reverseSf, {quarter}}},
  };
  auto lookup = [&](const std::string& id, TrackPoint& sf, std::vector<TrackPoint>& splits) {
    auto t = tracks.find(id);
    if (t == tracks.end()) return false;
    sf = t->second.first;
    splits = t->second.second;
    return true;
  };

  // The same fixes timed at the second layout's lines
  SessionReplay want([&](const std::string&, TrackPoint& sf, std::vector<TrackPoint>& splits) {
    return lookup("test.short", sf, splits);
  });
  replay(want, unmarked.text, 4096);
  CHECK(want.timer.lapCount() == 5, "second layout: %zu laps", want.timer.lapCount());
  CHECK(abs(want.timer.lap(0).splitMs[0] - s.live.lap(0).splitMs[0]) > 5000, "the layouts' splits should differ");

  {
    SessionReplay r(lookup);
    r.summaryTrackId = "test.short";
    replay(r, s.text, 4096);
    CHECK(r.switchedLayout && r.trackId == "test.short" && r.trackName.empty(), "switched: %s", r.trackId.c_str());
    checkSameLaps(r.timer, want.timer, 5, "switched layout", 0);
  }
  {  // the line was marked during the session: the header has none, the track list has it
    SessionReplay r(lookup);
    r.summaryTrackId = "test.short";
    replay(r, unmarked.text, 4096);
    CHECK(r.switchedLayout && r.trackId == "test.short", "switched, line from the track list");
    checkSameLaps(r.timer, want.timer, 5, "switched layout, line from the track list", 0);
  }
  // Timed as logged: no summary (or an unreadable one), the header's own
  // track, a track not in the list, or one at another line or the other way
  for (const char* id : {"", "test", "gone", "test.far", "test.reverse"}) {
    SessionReplay r(lookup);
    r.summaryTrackId = id;
    replay(r, s.text, 4096);
    CHECK(!r.switchedLayout && r.trackId == "test" && r.trackName == "Test Circle", "summary '%s': %s", id,
          r.trackId.c_str());
    checkSameLaps(r.timer, s.live, 5, id);
  }
  {  // no track list to look it up in
    SessionReplay r;
    r.summaryTrackId = "test.short";
    replay(r, s.text, 4096);
    CHECK(!r.switchedLayout && r.trackId == "test", "no track list");
    checkSameLaps(r.timer, s.live, 5, "no track list");
  }
  printf("  switched layout: re-timed as the summary's layout at the same line; another line or Reverse ignored\n");
}

// The start/finish's direction is recorded in the log, so a session is timed
// again as it was live: a line set the other way never counts, and one
// guessed backwards from the map is turned round in the replay as it was live.
static void testLineDirections() {
  TrackPoint q;
  const TrackPoint set = {45.1234567, -76.7654321, 12.5f, 271.3f, HeadingFrom::Set};
  CHECK(vbo::parsePoint(vbo::pointText(set), q) && q.headingFrom == HeadingFrom::Set &&
            fabs(q.heading - 271.3f) < 0.06f && fabs(q.lat - set.lat) < 1e-7 && fabs(q.lon - set.lon) < 1e-7 &&
            q.radius == 12.5f,
        "set direction: %s", vbo::pointText(set).c_str());
  TrackPoint guess = set;
  guess.headingFrom = HeadingFrom::Map;
  CHECK(vbo::parsePoint(vbo::pointText(guess), q) && q.headingFrom == HeadingFrom::Map, "guessed direction: %s",
        vbo::pointText(guess).c_str());
  TrackPoint none = set;
  none.headingFrom = HeadingFrom::None;
  CHECK(vbo::pointText(none) == "45.1234567 -76.7654321 12.5" && vbo::parsePoint(vbo::pointText(none), q) &&
            !q.hasHeading(),
        "no direction: %s", vbo::pointText(none).c_str());
  CHECK(!vbo::parsePoint("", q) && !vbo::parsePoint("45.1 x", q), "not a point");

  struct Case {
    const char* name;
    float heading;
    HeadingFrom from;
    size_t laps;  // timed live: the car crosses the line heading north
  };
  const Case cases[] = {{"set the right way", 0, HeadingFrom::Set, 5},
                        {"set the wrong way", 180, HeadingFrom::Set, 0},
                        {"guessed backwards", 180, HeadingFrom::Map, 5}};
  for (const Case& c : cases) {
    SessionFile s;
    makeSession(s, 37.0, -122.0, 86400000 - 120000, true, c.heading, c.from, 4000);
    CHECK(s.live.lapCount() == c.laps, "%s: %zu laps live, want %zu", c.name, s.live.lapCount(), c.laps);
    SessionReplay r;
    replay(r, s.text, 4096);
    checkSameLaps(r.timer, s.live, s.live.lapCount(), c.name, 2);
    printf("  line %s: %zu laps live, %zu re-timed from the log\n", c.name, s.live.lapCount(), r.timer.lapCount());
  }
}

// Free Roam: 2 km north at 72 km/h, 20 Hz, across UTC midnight.
static void testFreeRoamRecovery() {
  vbo::Header h;
  h.year = 2026;
  h.month = 10;
  h.day = 18;
  h.utcMs = 86400000 - 30000;
  h.comments = {"Track Rat GPS lap timer", "Started: 20261018_235930", "Type: freeroam"};
  std::string text = vbo::headerText(h);
  FreeRoam live;
  live.start(0);
  const int n = 2001;
  for (int i = 0; i < n; i++) {
    vbo::Sample smp;
    smp.utcMs = (h.utcMs + i * 50) % 86400000;
    smp.lat = 45.0 + i * 1.0 / 111194.93;
    smp.lon = -76.0;
    smp.speedKmh = 72;
    live.onFix(smp.lat, smp.lon, smp.speedKmh);
    text += line(smp);
  }
  SessionReplay r;
  replay(r, text, 4096);
  CHECK(r.freeRoam && !r.timed, "free roam log recognised");
  CHECK(fabs(r.drive.distanceKm() - live.distanceKm()) < 0.001 && fabs(r.drive.distanceKm() - 2.0) < 0.01,
        "distance %.4f km (live %.4f)", r.drive.distanceKm(), live.distanceKm());
  CHECK(fabs(r.drive.topSpeedKmh() - 72.0f) < 0.01f, "top speed %.2f", r.drive.topSpeedKmh());
  CHECK(r.durationMs == (n - 1) * 50, "duration %u", r.durationMs);
  printf("  free roam recovery: %.3f km, %u ms across midnight\n", r.drive.distanceKm(), r.durationMs);
}

// The writer's buffer with a producer and a consumer thread, as the main
// loop and the writer task use it: a session goes in line by line through
// a small ring (so it wraps and fills up), comes out byte for byte, and
// re-times the same.
static void testLogRingThreads() {
  SessionFile s1, s2;
  makeSession(s1, 37.0, -122.0, 86400000 - 120000);
  makeSession(s2, -33.8688, 151.2093, 1000);
  enum : uint8_t { OPEN = 1, DATA = 2, CLOSE = 3 };

  static uint8_t mem[8192];
  LogRing ring;
  ring.attach(mem, sizeof(mem));
  std::atomic<bool> done{false};
  std::map<std::string, std::string> files;
  std::atomic<int> waits{0};

  std::thread consumer([&] {
    std::string current;
    bool open = false;
    std::vector<char> buf(LogRing::MAX_RECORD);
    for (;;) {
      uint8_t type;
      size_t len;
      bool any = false;
      while (ring.next(type, len)) {
        any = true;
        ring.copyPayload(buf.data());
        if (type == OPEN) {
          current.assign(buf.data(), len);
          files[current] = "";
          open = true;
        } else if (type == DATA && open) {
          files[current].append(buf.data(), len);
        } else if (type == CLOSE) {
          open = false;
        }
        ring.pop();
      }
      if (!any) {
        if (done && !ring.next(type, len)) break;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
      }
    }
  });

  auto send = [&](uint8_t type, const char* p, size_t n) {
    while (!ring.push(type, p, n)) {  // full: on the device this line would be dropped; here, wait
      waits++;
      std::this_thread::yield();
    }
  };
  for (SessionFile* s : {&s1, &s2}) {
    const char* name = s == &s1 ? "/sessions/a.vbo" : "/sessions/b.vbo";
    send(OPEN, name, strlen(name));
    for (size_t off = 0; off < s->headerLen; off += LogRing::MAX_RECORD) {
      send(DATA, s->text.data() + off, std::min(LogRing::MAX_RECORD, s->headerLen - off));
    }
    for (int i = 0; i < s->fixes; i++) send(DATA, s->text.data() + s->headerLen + i * s->lineLen, s->lineLen);
    send(CLOSE, nullptr, 0);
  }
  done = true;
  consumer.join();

  CHECK(files.size() == 2, "%zu files written", files.size());
  CHECK(files["/sessions/a.vbo"] == s1.text, "file a differs (%zu vs %zu bytes)", files["/sessions/a.vbo"].size(),
        s1.text.size());
  CHECK(files["/sessions/b.vbo"] == s2.text, "file b differs");
  SessionReplay r;
  replay(r, files["/sessions/a.vbo"], 4096);
  checkSameLaps(r.timer, s1.live, 5, "through the ring");
  printf("  log buffer, 2 threads: 2 sessions (%zu KB) through an 8 KB ring byte for byte, %d waits when full\n",
         (s1.text.size() + s2.text.size()) / 1024, (int)waits);

  // Full: pushes fail cleanly, and work again once there's room
  LogRing small;
  static uint8_t m2[256];
  small.attach(m2, sizeof(m2));
  int pushed = 0;
  char l[91] = {};
  while (small.push(DATA, l, sizeof(l))) pushed++;
  CHECK(pushed == 2 && small.freeSpace() < sizeof(l) + LogRing::RECORD_HEADER, "pushed %d into 256 bytes", pushed);
  uint8_t type;
  size_t len;
  CHECK(small.next(type, len) && type == DATA && len == sizeof(l), "record header");
  small.pop();
  CHECK(small.push(DATA, l, sizeof(l)), "room again after a pop");
  CHECK(!small.push(DATA, l, LogRing::MAX_RECORD + 1), "oversized record refused");
}

// Engine channels (OBD-II over CAN or Bluetooth): five more columns, only in logs that have them,
// and a log with them still reads back and times the same
static void testEngineChannels() {
  vbo::Header h;
  h.engine = true;
  const std::string text = vbo::headerText(h);
  CHECK(text.find("RPM\r\nThrottle position\r\nCoolant temperature\r\nIntake temperature\r\nOil temperature\r\n") != std::string::npos,
        "engine channels in [header]");
  CHECK(text.find("rpm\r\n%\r\nC\r\nC\r\nC\r\n") != std::string::npos, "and their units");
  CHECK(text.find("LapTag RPM Throttle CoolantTemp IntakeTemp OilTemp\r\n") != std::string::npos, "and column names");
  vbo::Header plain;
  CHECK(vbo::headerText(plain).find("RPM") == std::string::npos, "not without");

  vbo::Sample s;
  s.utcMs = 36000000;
  s.lat = 45.1;
  s.lon = -76.2;
  s.speedKmh = 120;
  s.rpm = 6512.4f;
  s.throttlePct = 87.25f;
  s.coolantC = 92;
  s.intakeC = 31.5f;
  s.oilC = 121;
  char a[160], b[160];
  const size_t na = vbo::formatLine(s, a, sizeof(a)), nb = vbo::formatLine(s, b, sizeof(b), true);
  CHECK(na && nb == na + 33, "33 more characters: %zu vs %zu", nb, na);
  CHECK(std::string(b, nb).find(" 06512 087.2 +092.0 +031.5 +121.0\r\n") != std::string::npos, "engine values: %s", b);
  CHECK(vbo::formatLine(s, b, nb, true) == 0 && vbo::formatLine(s, b, nb + 1, true) == nb, "no partial lines");
  s.rpm = 0;
  s.coolantC = -40;
  CHECK(vbo::formatLine(s, b, sizeof(b), true) == nb, "same length when unknown or cold");

  // Read back: the extra columns don't get in the way
  vbo::Reader r;
  vbo::Sample out;
  std::string all = text;
  s.rpm = 3000;
  all.append(b, vbo::formatLine(s, b, sizeof(b), true));
  size_t start = 0;
  bool got = false;
  while (start < all.size()) {
    size_t end = all.find("\r\n", start);
    if (end == std::string::npos) break;
    got = r.feed(all.substr(start, end - start).c_str(), out) || got;
    start = end + 2;
  }
  CHECK(got && fabs(out.lat - 45.1) < 1e-6 && fabs(out.speedKmh - 120) < 0.01, "read back with engine columns");
  CHECK(r.hasCoolant() && r.hasOil() && fabs(out.coolantC + 40) < 0.01 && fabs(out.oilC - 121) < 0.01,
        "coolant and oil read back (%.1f, %.1f)", out.coolantC, out.oilC);

  // A log from before oil temperature (four engine columns): coolant still reads, oil isn't there
  std::string old = text;
  for (const char* from : {"Intake temperature\r\nOil temperature\r\n", "C\r\nC\r\nC\r\n", "IntakeTemp OilTemp"}) {
    const std::string f = from, to = f == "Intake temperature\r\nOil temperature\r\n" ? "Intake temperature\r\n"
                                     : f == "C\r\nC\r\nC\r\n" ? "C\r\nC\r\n" : "IntakeTemp";
    old.replace(old.find(f), f.size(), to);
  }
  old += "000 100000.000 02706.00000 04572.00000 120.000 000.00 +00000.00 00.00 001 +0.000 +0.000 000 03000 087.2 +095.0 +031.5\r\n";
  vbo::Reader r4;
  got = false;
  for (size_t st = 0, en; (en = old.find("\r\n", st)) != std::string::npos; st = en + 2)
    got = r4.feed(old.substr(st, en - st).c_str(), out) || got;
  CHECK(got && r4.hasCoolant() && !r4.hasOil() && fabs(out.coolantC - 95) < 0.01 && out.oilC == 0, "four-column log: coolant %.1f",
        out.coolantC);
  vbo::Reader plainR;
  plainR.feed("[header]", out);
  plainR.feed("time", out);
  CHECK(!plainR.hasCoolant() && !plainR.hasOil(), "no engine columns");
}

int main() {
  printf("VBO log tests\n");
  testEngineChannels();
  testCoordinates();
  testTime();
  testGForce();
  testGate();
  testHemispheres();
  testRecovery();
  testSwitchedLayoutRecovery();
  testLineDirections();
  testFreeRoamRecovery();
  testLogRingThreads();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
