/**
 * Track Rat - Adjust start / Adjust finish on the device: what to keep from a log, and the screen's model (9.3b)
 *
 * Moving a course's start or finish in 0.5 m steps and seeing every run of the day re-timed as the
 * line moves. The maths is LineNudge.h (9.3a); this is what the screen needs around it, with no Arduino
 * code, tested on a PC in test/test_adjustline.cpp:
 *
 * - Which logs: the .vbo files of the day's sessions at the course (the names CourseRuns.h reads the
 *   summaries by). logsForDay().
 * - Reading a log (LogScan): the bytes go through in any chunk size and only what the screen needs is
 *   kept. For each run of the session (the log's lap column): the fixes near the line being moved
 *   (Slicer is sliceNear() done as the fixes arrive, and a car waiting at the line adds two fixes, not
 *   thousands), the other end's crossing time, and for a standing start the fix it stood at and when it
 *   moved off. A run that was not timed (it ended without a finish) is dropped, so the numbers are the
 *   hub's run numbers. DayScan collects a day's sessions and keeps the latest MAX_RUNS runs.
 * - The model (Model): the line's offset along its direction of travel, every run's time at that
 *   offset, the old times, the best, and the View the screen draws.
 * - Buttons greyed out, with a reason (block()); the standing-start note.
 *
 * A run's time follows LapTimer: a rolling start begins at the crossing, a standing start when the
 * car moves off, as long as the car stood no more than STAGE_LIGHTS_M behind the line and within
 * STAGE_ARM_M of it (linenudge::standingStartHolds). A start line with no direction can only start
 * standing starts, so it times nothing for a car that is no longer "at" it.
 */

#ifndef ADJUST_LINE_H
#define ADJUST_LINE_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <vector>
#include "AdjustView.h"
#include "Config.h"
#include "Courses.h"
#include "GeoMath.h"
#include "LineNudge.h"
#include "VboFormat.h"

namespace adjust {

using linenudge::Fix;
using linenudge::Line;
using linenudge::NONE;

enum class Which : uint8_t { Start, Finish };

constexpr size_t MAX_RUNS = 20;             // runs kept (the latest): about 4 KB each
constexpr double LIMIT_M = 15.0;            // the furthest a line can be moved either way
constexpr double VIEW_HALF_M = 18.0;        // half the width of the zoomed view
constexpr size_t MAX_SLICE_FIXES = 3000;    // per line per log (about 100 KB); more is dropped
constexpr int64_t WINDOW_BEFORE_MS = 15000; // a run's fixes near its start line: this long before its first fix...
constexpr int64_t WINDOW_AFTER_MS = 10000;  // ...to this long after
constexpr int64_t FIN_BEFORE_MS = 10000;    // and near its finish line: this long before its last fix...
constexpr int64_t FIN_AFTER_MS = 15000;     // ...to this long after it (the crossing is just after the last fix)

// --- Which logs ---------------------------------------------------------------

// Does this file in /sessions hold the full log of a session of this course on this day?
// Logs are named like their summaries, with ".vbo".
inline bool logFor(const std::string& fileName, const std::string& trackId, const std::string& event) {
  if (fileName.size() < 5 || fileName.compare(fileName.size() - 4, 4, ".vbo") != 0) return false;
  return courses::summaryFor(fileName.substr(0, fileName.size() - 4) + ".json", trackId, event);
}

// The day's logs, in the order they were driven
inline std::vector<std::string> logsForDay(const std::vector<std::string>& names, const std::string& trackId,
                                           const std::string& event) {
  std::vector<std::string> out;
  for (const std::string& n : names)
    if (logFor(n, trackId, event)) out.push_back(n);
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
    const size_t sa = a.rfind('/'), sb = b.rfind('/');
    return a.substr(sa == std::string::npos ? 0 : sa + 1) < b.substr(sb == std::string::npos ? 0 : sb + 1);
  });
  return out;
}

// --- Keeping the fixes near a line ---------------------------------------------

// linenudge::sliceNear() for fixes that arrive one at a time. Gives the same fixes, at most
// MAX_SLICE_FIXES of them.
class Slicer {
public:
  void begin(double cx, double cy, double radiusM = linenudge::NEAR_LINE_M) {
    this->cx = cx;
    this->cy = cy;
    r = radiusM;
    out.clear();
    have = false;
    prevNear = false;
    idx = 0;
    lastKept = -2;
    full = false;
  }
  void add(const Fix& f) {
    const bool n = hypot(f.x - cx, f.y - cy) <= r;
    if (have) settle(n);
    pend = f;
    pendNear = n;
    have = true;
  }
  // After the last fix
  void end() {
    if (have) settle(false);
    have = false;
  }
  const std::vector<Fix>& fixes() const { return out; }
  bool truncated() const { return full; }
  // Forgets the fixes older than ms (a running scan only needs the last few seconds)
  void dropBefore(int64_t ms) {
    size_t n = 0;
    while (n < out.size() && out[n].ms < ms) n++;
    if (n) out.erase(out.begin(), out.begin() + n);
  }
  void release() { std::vector<Fix>().swap(out); }

private:
  double cx = 0, cy = 0, r = linenudge::NEAR_LINE_M;
  std::vector<Fix> out;
  Fix pend;
  bool have = false, pendNear = false, prevNear = false, full = false;
  long idx = 0, lastKept = -2;
  void settle(bool nextNear) {
    if (prevNear || pendNear || nextNear) {
      if (out.size() < MAX_SLICE_FIXES) {
        pend.joined = lastKept == idx - 1;
        out.push_back(pend);
        lastKept = idx;
      } else {
        full = true;
      }
    }
    prevNear = pendNear;
    idx++;
  }
};

// The fixes of a slice with GPS times from..to, joined to each other as they were (the first isn't)
inline std::vector<Fix> window(const std::vector<Fix>& s, int64_t fromMs, int64_t toMs) {
  std::vector<Fix> out;
  for (const Fix& f : s)
    if (f.ms >= fromMs && f.ms <= toMs) out.push_back(f);
  if (!out.empty()) out[0].joined = false;
  return out;
}

// --- One run ------------------------------------------------------------------

struct RunData {
  int number = 0;            // the day's run number (the hub's)
  std::vector<Fix> near;     // the fixes near the line being moved, around this run's crossing of it
  int64_t fixedMs = NONE;    // the other line's crossing: the finish (Adjust start) or the start (Adjust finish; NONE if standing)
  bool standing = false;     // Adjust finish: this run was a standing start
  bool hasStood = false;     // Adjust start: the car stood still near the line before moving off
  Fix stood;                 // ...here (the last fix slower than STAGE_STOPPED_KMH)
  int64_t moveOffMs = NONE;  // ...and a standing start's clock starts at this time
  int64_t minMs = 0;         // the shortest run that counts
};

// --- Reading one log -----------------------------------------------------------

struct Setup {
  Which which = Which::Finish;
  TrackPoint start = {0, 0, 0, 0, HeadingFrom::None};
  bool hasFinish = false;
  TrackPoint finish = {0, 0, 0, 0, HeadingFrom::None};
};

inline double gateHalf(const TrackPoint& p) { return p.radius > 0 ? p.radius : 10.0; }

// One session's log, fed as raw bytes. Each run's fixes near the lines are taken as soon as the run is
// over (and the seconds after it, where the finish crossing is), so the memory held while reading is a
// few seconds of fixes, not the session.
class LogScan {
public:
  // Runs are appended to `out` (the latest MAX_RUNS stay), numbered on from `counted`.
  void begin(const Setup& s, std::vector<RunData>* out, int* counted) {
    setup = s;
    out_ = out;
    counted_ = counted;
    refLat = s.start.lat;
    refLon = s.start.lon;
    startL = linenudge::lineAt(s.start, refLat, refLon);
    finL = s.hasFinish ? linenudge::lineAt(s.finish, refLat, refLon) : Line();
    startHw = gateHalf(s.start);
    finHw = gateHalf(s.finish);
    sStart.begin(startL.x, startL.y);
    sFin.begin(finL.x, finL.y);
    rdr = vbo::Reader();
    len = 0;
    overlong = false;
    haveRaw = havePrev = haveStopped = haveStill = false;
    dayOffset = 0;
    curLap = 0;
    marks.clear();
    nextMark = 0;
    held = lastWasStill = false;
    minLap = 0;
    sinceTrim = 0;
  }

  void feed(const char* data, size_t n) {
    for (size_t i = 0; i < n; i++) {
      const char c = data[i];
      if (c == '\n') endLine();
      else if (len < MAX_LINE) line[len++] = c;
      else overlong = true;
    }
  }

  // After the last byte. (A last line without its line ending was cut short by a power cut: dropped.)
  void end() {
    len = 0;
    overlong = false;
    flushHeld();
    sStart.end();
    sFin.end();
    for (; nextMark < marks.size(); nextMark++) take(marks[nextMark]);
    marks.clear();
    nextMark = 0;
    sStart.release();
    sFin.release();
  }

  bool truncated() const { return sStart.truncated() || sFin.truncated(); }

private:
  struct Mark {
    int64_t firstMs = 0, lastMs = 0, moveOffMs = NONE;
    bool hasStood = false, ended = false;
    Fix stood;
  };
  static const size_t MAX_LINE = 400;
  Setup setup;
  std::vector<RunData>* out_ = nullptr;
  int* counted_ = nullptr;
  double refLat = 0, refLon = 0, startHw = 10, finHw = 10;
  Line startL, finL;
  Slicer sStart, sFin;
  vbo::Reader rdr;
  char line[MAX_LINE + 1];
  size_t len = 0;
  bool overlong = false;
  bool haveRaw = false;
  uint32_t lastRaw = 0;
  int64_t dayOffset = 0;
  Fix prev;
  bool havePrev = false;
  bool haveStopped = false, haveStill = false;
  Fix stopped;
  int64_t stillMs = 0;
  int curLap = 0;
  std::vector<Mark> marks;
  size_t nextMark = 0;  // marks before this are taken
  Fix heldFix;          // a fix standing still, after the first of its stretch: kept only if it is the last
  bool held = false, lastWasStill = false;
  long minLap = 0;
  int sinceTrim = 0;

  void endLine() {
    line[len] = 0;
    vbo::Sample s;
    if (!overlong && rdr.feed(line, s)) onSample(s);
    len = 0;
    overlong = false;
  }

  // Standing still adds the first fix and the last of its stretch to the slices, nothing between
  void sliceAdd(const Fix& f) {
    const bool still = f.kmh < STAGE_STILL_KMH;
    if (still && lastWasStill) {
      heldFix = f;
      held = true;
      return;
    }
    flushHeld();
    sStart.add(f);
    sFin.add(f);
    lastWasStill = still;
  }
  void flushHeld() {
    if (!held) return;
    sStart.add(heldFix);
    sFin.add(heldFix);
    held = false;
  }

  // The run's times with the lines as they are; kept if it is a timed run. Needs the fixes up to
  // WINDOW_AFTER_MS after the run's start and FIN_AFTER_MS after its end.
  void take(const Mark& m) {
    if (minLap <= 0) {
      minLap = atol(rdr.comment("Min lap ms").c_str());
      if (minLap <= 0) minLap = MIN_LAP_TIME_MS;
      if (minLap == MIN_LAP_TIME_MS) minLap = STAGE_MIN_RUN_MS;  // (as LapTimer::setStage)
    }
    const std::vector<Fix> sw = window(sStart.fixes(), m.firstMs - WINDOW_BEFORE_MS, m.firstMs + WINDOW_AFTER_MS);
    const std::vector<Fix> fw = window(sFin.fixes(), m.lastMs - FIN_BEFORE_MS, m.lastMs + FIN_AFTER_MS);
    const bool standing = m.hasStood && linenudge::standingStartHolds(m.stood, startL);
    int64_t startMs = NONE;
    if (!standing && startL.hasHeading) startMs = linenudge::crossingTimeMs(sw, startL, startHw);
    const int64_t from = standing ? m.moveOffMs : startMs;
    const int64_t finMs = setup.hasFinish ? linenudge::crossingTimeMs(fw, finL, finHw, from) : NONE;
    if (linenudge::newRunMs(startMs, finMs, standing, m.moveOffMs, minLap) == NONE) return;  // not a timed run

    RunData r;
    r.number = ++*counted_;
    r.minMs = minLap;
    r.moveOffMs = m.moveOffMs;
    if (setup.which == Which::Start) {
      r.near = sw;
      r.fixedMs = finMs;
      r.hasStood = m.hasStood;
      r.stood = m.stood;
    } else {
      r.near = fw;
      r.fixedMs = standing ? NONE : startMs;
      r.standing = standing;
    }
    r.near.shrink_to_fit();
    out_->push_back(std::move(r));
    if (out_->size() > MAX_RUNS) out_->erase(out_->begin());  // the latest are kept
  }

  // Take the runs whose last needed fix has gone by, then forget fixes no run to come will want
  void taken(int64_t nowMs) {
    while (nextMark < marks.size() && marks[nextMark].ended && nowMs > marks[nextMark].lastMs + FIN_AFTER_MS &&
           nowMs > marks[nextMark].firstMs + WINDOW_AFTER_MS)
      take(marks[nextMark++]);
    if (nextMark == marks.size() && nextMark > 0) {
      marks.clear();
      nextMark = 0;
    }
    if (++sinceTrim < 200) return;
    sinceTrim = 0;
    int64_t keepFrom = nowMs - WINDOW_BEFORE_MS - 5000;
    if (nextMark < marks.size()) keepFrom = std::min(keepFrom, marks[nextMark].firstMs - WINDOW_BEFORE_MS);
    sStart.dropBefore(keepFrom);
    sFin.dropBefore(keepFrom);
  }

  void onSample(const vbo::Sample& s) {
    if (haveRaw && s.utcMs + 43200000UL < lastRaw) dayOffset += 86400000LL;  // past UTC midnight
    haveRaw = true;
    lastRaw = s.utcMs;
    const int64_t ms = dayOffset + s.utcMs;
    const Fix f = linenudge::fixAt(s.lat, s.lon, ms, s.speedKmh, refLat, refLon);

    if ((int)s.lap != curLap) {
      if (curLap > 0 && !marks.empty()) marks.back().ended = true;
      if (s.lap > 0) {  // a run begins on this fix
        Mark m;
        m.firstMs = m.lastMs = ms;
        m.hasStood = haveStopped;
        m.stood = stopped;
        // The clock starts from the last fix standing still, unless that was over 3 s ago (LapTimer::onFix)
        m.moveOffMs = haveStill && ms - stillMs <= 3000 ? stillMs : havePrev ? prev.ms : ms;
        marks.push_back(m);
        haveStopped = haveStill = false;
      }
      curLap = s.lap;
    } else if (s.lap > 0 && !marks.empty()) {
      marks.back().lastMs = ms;
    }
    if (s.lap == 0) {
      if (f.kmh < STAGE_STOPPED_KMH) {
        stopped = f;
        haveStopped = true;
      }
      if (f.kmh < STAGE_STILL_KMH) {
        stillMs = ms;
        haveStill = true;
      }
    }
    sliceAdd(f);
    prev = f;
    havePrev = true;
    taken(ms);
  }
};

// A day's logs, one after another
class DayScan {
public:
  void begin(const Setup& s) {
    setup = s;
    runs_.clear();
    counted_ = 0;
    logs_ = 0;
    truncated_ = false;
  }
  void beginLog() { scan.begin(setup, &runs_, &counted_); }
  void feed(const char* data, size_t n) { scan.feed(data, n); }
  void endLog() {
    scan.end();
    truncated_ = truncated_ || scan.truncated();
    logs_++;
  }
  std::vector<RunData>& runs() { return runs_; }
  int counted() const { return counted_; }  // timed runs seen, kept or not
  int logs() const { return logs_; }
  bool truncated() const { return truncated_; }

private:
  Setup setup;
  LogScan scan;
  std::vector<RunData> runs_;
  int counted_ = 0, logs_ = 0;
  bool truncated_ = false;
};

// --- Text ----------------------------------------------------------------------

// "52.31", "1:02.31" (hundredths, cut not rounded, as the clock); "-" for none
inline std::string timeText(int64_t ms) {
  if (ms == NONE || ms < 0) return "-";
  char b[24];
  const long cs = (long)((ms / 10) % 100), s = (long)((ms / 1000) % 60), m = (long)(ms / 60000);
  if (m) snprintf(b, sizeof(b), "%ld:%02ld.%02ld", m, s, cs);
  else snprintf(b, sizeof(b), "%ld.%02ld", s, cs);
  return b;
}

// "+0.06", "-0.12"; "" if there is no change at the hundredth (or a time is missing)
inline std::string diffText(int64_t oldMs, int64_t newMs) {
  if (oldMs == NONE || newMs == NONE) return "";
  const int64_t d = newMs - oldMs;
  const int64_t cs = (llabs(d) + 5) / 10;
  if (cs == 0) return "";
  char b[24];
  snprintf(b, sizeof(b), "%c%lld.%02lld", d < 0 ? '-' : '+', (long long)(cs / 100), (long long)(cs % 100));
  return b;
}

// "Moved +1.5 m (later)", "Moved -0.5 m (earlier)", "Not moved"
inline std::string offsetText(double m) {
  if (fabs(m) < 0.05) return "Not moved";
  char b[40];
  snprintf(b, sizeof(b), "Moved %+.1f m (%s)", m, m > 0 ? "later" : "earlier");
  return b;
}

// What to tell the driver about standing starts when the start line is moved
inline std::string standingNote(const Line& start) {
  char b[200];
  if (start.hasHeading)
    snprintf(b, sizeof(b),
             "Standing runs are timed from moving off. They only change if the line moves more than %.0f m ahead of where the car stood.",
             STAGE_LIGHTS_M);
  else
    snprintf(b, sizeof(b),
             "Standing runs are timed from moving off. They only change if the line is moved more than %.0f m from where the car stood.",
             STAGE_ARM_M);
  return b;
}

// --- Whether the button can be used ---------------------------------------------

enum class Block : uint8_t { None, NoCard, Retiming, NoStart, NoFinish, Reading, NoRuns };

inline Block block(Which w, bool hasStart, bool hasFinish, int runs, bool readingRuns, bool card, bool retiming) {
  if (!card) return Block::NoCard;
  if (retiming) return Block::Retiming;
  if (w == Which::Start && !hasStart) return Block::NoStart;
  if (!hasFinish) return Block::NoFinish;  // (no run can be timed without one either)
  if (readingRuns) return Block::Reading;
  if (runs <= 0) return Block::NoRuns;
  return Block::None;
}

// Short enough for the hub's button caption
inline const char* blockText(Block b) {
  switch (b) {
    case Block::NoCard: return "needs SD card";
    case Block::Retiming: return "re-timing...";
    case Block::NoStart: return "no start set";
    case Block::NoFinish: return "set finish first";
    case Block::Reading: return "reading runs...";
    case Block::NoRuns: return "needs a run";
    default: return "";
  }
}

// --- The screen's model ----------------------------------------------------------

class Model {
public:
  // `line`: the start or finish being moved, as stored. `runs`: from DayScan, whose fixes are in metres from
  // the START line (refLat/refLon is the start's place).
  void setup(Which w, const TrackPoint& line, double refLat, double refLon, std::vector<RunData>&& runs) {
    which = w;
    stored = line;
    hw = gateHalf(line);
    line0 = linenudge::lineAt(line, refLat, refLon);
    runs_ = std::move(runs);
    offset_ = 0;
    dirDeg = line0.hasHeading ? line0.heading : NAN;
    if (isnan(dirDeg))  // no direction of its own: the way the runs cross it
      for (const RunData& r : runs_) {
        dirDeg = linenudge::pathHeadingDeg(r.near, line0, hw);
        if (!isnan(dirDeg)) break;
      }
    old_.assign(runs_.size(), NONE);
    cur_.assign(runs_.size(), NONE);
    for (size_t i = 0; i < runs_.size(); i++) old_[i] = cur_[i] = timeAt(runs_[i], 0);
    buildPaths();
  }

  Which kind() const { return which; }
  size_t size() const { return runs_.size(); }
  const RunData& run(size_t i) const { return runs_[i]; }
  double offsetM() const { return offset_; }
  bool canMove() const { return !isnan(dirDeg); }
  float directionDeg() const { return dirDeg; }
  const Line& line() const { return line0; }
  int64_t oldMs(size_t i) const { return old_[i]; }
  int64_t newMs(size_t i) const { return cur_[i]; }

  // Moves the line by `steps` of 0.5 m (negative: earlier), within LIMIT_M. False if nothing changed.
  bool nudge(int steps) {
    if (!canMove()) return false;
    double o = offset_ + steps * linenudge::STEP_M;
    o = std::max(-LIMIT_M, std::min(LIMIT_M, o));
    o = floor(o / linenudge::STEP_M + 0.5) * linenudge::STEP_M;
    if (o == offset_) return false;
    offset_ = o;
    recompute();
    return true;
  }
  bool reset() {
    if (offset_ == 0) return false;
    offset_ = 0;
    recompute();
    return true;
  }

  // The stored point moved by the offset: what Save writes
  TrackPoint moved() const {
    TrackPoint p = stored;
    if (offset_ != 0) linenudge::shiftPoint(p, offset_, dirDeg);
    return p;
  }

  // The run that is quickest with the line as it is now (-1: none)
  int bestIndex() const {
    int b = -1;
    for (size_t i = 0; i < cur_.size(); i++)
      if (cur_[i] != NONE && (b < 0 || cur_[i] < cur_[b])) b = (int)i;
    return b;
  }

  // Fills what the screen shows (the paths only once: they do not depend on the offset)
  void fillView(View& v) const {
    v.finish = which == Which::Finish;
    v.phase = Phase::Ready;
    v.offsetM = (float)offset_;
    v.halfWidthM = (float)hw;
    v.canMove = canMove();
    v.atEarliest = offset_ <= -LIMIT_M;
    v.atLatest = offset_ >= LIMIT_M;
    v.offsetText = canMove() ? offsetText(offset_) : "Can't tell which way is forward";
    if (v.paths.size() != runs_.size()) v.paths = paths_;
    v.rows.clear();
    const int best = bestIndex();
    int oldBest = -1;
    for (size_t i = 0; i < old_.size(); i++)
      if (old_[i] != NONE && (oldBest < 0 || old_[i] < old_[oldBest])) oldBest = (int)i;
    for (size_t i = 0; i < runs_.size(); i++) {
      ViewRow r;
      r.number = runs_[i].number;
      r.oldText = timeText(old_[i]);
      r.newText = timeText(cur_[i]);
      r.diffText = diffText(old_[i], cur_[i]);
      r.changed = old_[i] != cur_[i];
      r.best = (int)i == best;
      v.rows.push_back(r);
    }
    v.summary.clear();
    if (best >= 0) {
      v.summary = "Best " + timeText(cur_[best]) + " (run " + std::to_string(runs_[best].number) + ")";
      if (oldBest >= 0 && cur_[best] != old_[oldBest]) v.summary = "Best " + timeText(old_[oldBest]) + " > " + timeText(cur_[best]) + " (run " + std::to_string(runs_[best].number) + ")";
    }
    v.note.clear();
    if (which == Which::Start) {
      bool anyStanding = false;
      for (const RunData& r : runs_) anyStanding = anyStanding || (r.hasStood && linenudge::standingStartHolds(r.stood, line0));
      if (anyStanding) v.note = standingNote(line0);
    }
  }

  // A run's time with the line `offset` metres along (NONE if it can't be timed there)
  int64_t timeAt(const RunData& r, double offset) const {
    Line l = line0;
    if (offset != 0) linenudge::shift(l, offset, dirDeg);
    if (which == Which::Finish) {
      const int64_t from = r.standing ? r.moveOffMs : r.fixedMs;
      const int64_t fin = linenudge::crossingTimeMs(r.near, l, hw, from);
      return linenudge::newRunMs(r.fixedMs, fin, r.standing, r.moveOffMs, r.minMs);
    }
    const bool standing = r.hasStood && linenudge::standingStartHolds(r.stood, l);
    int64_t st = NONE;
    if (!standing) {
      if (!line0.hasHeading) return NONE;  // a start with no direction only starts standing starts
      st = linenudge::crossingTimeMs(r.near, l, hw);
    }
    return linenudge::newRunMs(st, r.fixedMs, standing, r.moveOffMs, r.minMs);
  }

private:
  Which which = Which::Finish;
  TrackPoint stored = {0, 0, 0, 0, HeadingFrom::None};
  Line line0;
  double hw = 10;
  float dirDeg = NAN;
  double offset_ = 0;
  std::vector<RunData> runs_;
  std::vector<int64_t> old_, cur_;
  std::vector<std::vector<PathPt>> paths_;

  void recompute() {
    for (size_t i = 0; i < runs_.size(); i++) cur_[i] = timeAt(runs_[i], offset_);
  }
  void buildPaths() {
    paths_.assign(runs_.size(), {});
    if (isnan(dirDeg)) return;
    const double h = dirDeg * geo::DEG2RAD;
    for (size_t i = 0; i < runs_.size(); i++)
      for (const Fix& f : runs_[i].near) {
        const double dx = f.x - line0.x, dy = f.y - line0.y;
        const double along = dx * sin(h) + dy * cos(h), across = dx * cos(h) - dy * sin(h);
        if (fabs(along) <= VIEW_HALF_M + 3 && fabs(across) <= 30) paths_[i].push_back({(float)along, (float)across});
      }
  }
};

}  // namespace adjust

#endif  // ADJUST_LINE_H
