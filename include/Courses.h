/**
 * Track Rat - autocross courses as events (9.2)
 *
 * A course is a point-to-point track ("type": "stage") made by the Autocross
 * hub's New course. It is saved with "kind": "course" and "event":
 * "YYYYMMDD", the local date it was made, and named "Oct 25 course" ("Oct 25
 * course 2" for a second one that day). Courses stay out of the circuit
 * list; the hub and the boot auto-pick find them here. Older stages named
 * "Course 2026-10-20" (from the old NEW TRACK > Course) count as courses
 * with that date.
 *
 * The day's runs are one list, numbered 1, 2, 3... across every session of
 * the course on its event date, whatever happened between them (a power-off,
 * a session ending). They are read from the day's session summaries
 * (summaryFor picks the files), and the run being timed now follows them.
 *
 * Pure C++ (no Arduino); tested on a PC in test/test_courses.cpp.
 */

#ifndef COURSES_H
#define COURSES_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include "Config.h"
#include "GeoMath.h"
#include "LapTags.h"

namespace courses {

constexpr double NEAR_M = 5000.0;  // "nearby": the same distance as the boot auto-pick (TRACK_DETECTION_RADIUS_M)

// "20261020" if s is eight digits (a stamp "20261020_141500" has its date first), else ""
inline std::string dateOfStamp(const std::string& s) {
  if (s.size() < 8) return "";
  for (int i = 0; i < 8; i++)
    if (s[i] < '0' || s[i] > '9') return "";
  if (s.size() > 8 && s[8] != '_') return "";
  return s.substr(0, 8);
}

// A valid calendar date in YYYYMMDD form (year 2020 on, month 1-12, day 1-31)
inline bool validEvent(const std::string& e) {
  if (e.size() != 8) return false;
  for (char c : e)
    if (c < '0' || c > '9') return false;
  const int y = atoi(e.substr(0, 4).c_str()), m = atoi(e.substr(4, 2).c_str()), d = atoi(e.substr(6, 2).c_str());
  return y >= 2020 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

// The event date of a stage made by the old NEW TRACK > Course: "Course 2026-10-20" or
// "Course 2026-10-20 (2)" -> "20261020". "" for any other name.
inline std::string eventFromName(const std::string& name) {
  if (name.compare(0, 7, "Course ") != 0 || name.size() < 17) return "";
  const std::string d = name.substr(7, 10);  // 2026-10-20
  if (d[4] != '-' || d[7] != '-') return "";
  const std::string e = d.substr(0, 4) + d.substr(5, 2) + d.substr(8, 2);
  if (!validEvent(e)) return "";
  const std::string rest = name.substr(17);
  if (!rest.empty()) {  // only " (2)" may follow
    if (rest.size() < 4 || rest.compare(0, 2, " (") != 0 || rest.back() != ')') return "";
    for (size_t i = 2; i + 1 < rest.size(); i++)
      if (rest[i] < '0' || rest[i] > '9') return "";
  }
  return e;
}

// What tracks.json says about a stage: is it a course, and on which day?
// kind and event are its "kind" and "event" ("" if absent). A stage with no kind is a hill
// climb or sprint, unless it is named like an old course.
struct Parsed {
  bool course = false;
  std::string event;  // "" if not known
};
inline Parsed classify(bool stage, const std::string& kind, const std::string& event, const std::string& name) {
  Parsed p;
  if (!stage) return p;
  if (kind == "course") {
    p.course = true;
    p.event = validEvent(event) ? event : eventFromName(name);
  } else if (kind.empty()) {
    p.event = eventFromName(name);
    p.course = !p.event.empty();
  }
  return p;
}

// "Oct 25" for 20261025 ("" if not a date)
inline std::string shortDate(const std::string& event) {
  static const char* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (!validEvent(event)) return "";
  return std::string(MONTHS[atoi(event.substr(4, 2).c_str()) - 1]) + " " + std::to_string(atoi(event.substr(6, 2).c_str()));
}

inline bool contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

// A new course's name: "Oct 25 course", then "Oct 25 course 2", "... 3" if that day already has
// one (taken: every track name). With no date: "New course", "New course 2"...
inline std::string courseName(const std::string& event, const std::vector<std::string>& taken) {
  const std::string base = validEvent(event) ? shortDate(event) + " course" : std::string("New course");
  if (!contains(taken, base)) return base;
  for (int n = 2;; n++) {
    const std::string name = base + " " + std::to_string(n);
    if (!contains(taken, name)) return name;
  }
}

// Its id for tracks.json: "course-20261025", "course-20261025-2"... (taken: every venue id)
inline std::string courseId(const std::string& event, const std::vector<std::string>& taken) {
  const std::string base = std::string("course-") + (validEvent(event) ? event : std::string("new"));
  if (!contains(taken, base)) return base;
  for (int n = 2;; n++) {
    const std::string id = base + "-" + std::to_string(n);
    if (!contains(taken, id)) return id;
  }
}

// --- Which course -------------------------------------------------------------

struct Candidate {
  int index;          // the caller's own number for it (the track's index)
  std::string event;  // "YYYYMMDD", "" if not known
  double lat, lon;    // its start
};

// Today's course nearby: the nearest course made today within maxM of here. Its index in `list`
// (not Candidate::index), or -1. today "" (the date isn't known): none.
inline int todaysNearby(const std::vector<Candidate>& list, const std::string& today, double lat, double lon,
                        double maxM = NEAR_M) {
  int best = -1;
  double bestD = maxM;
  if (!validEvent(today)) return -1;
  for (size_t i = 0; i < list.size(); i++) {
    if (list[i].event != today) continue;
    const double d = geo::distanceM(lat, lon, list[i].lat, list[i].lon);
    if (d < bestD) bestD = d, best = (int)i;
  }
  return best;
}

// The hub's course: today's nearby, else the most recent course within maxM (latest event date;
// the nearer on the same date), else -1. A course with no known date is the oldest.
inline int forHub(const std::vector<Candidate>& list, const std::string& today, double lat, double lon,
                  double maxM = NEAR_M) {
  const int t = todaysNearby(list, today, lat, lon, maxM);
  if (t >= 0) return t;
  int best = -1;
  double bestD = 0;
  for (size_t i = 0; i < list.size(); i++) {
    const double d = geo::distanceM(lat, lon, list[i].lat, list[i].lon);
    if (d >= maxM) continue;
    if (best < 0 || list[i].event > list[best].event || (list[i].event == list[best].event && d < bestD))
      best = (int)i, bestD = d;
  }
  return best;
}

// --- The day's runs ------------------------------------------------------------

struct Run {
  uint32_t timeMs = 0;
  uint8_t tags = 0;  // laptags::
};

// The tag bits for a summary's tag names ("pit", "slow", "suspect"); anything else adds nothing
inline uint8_t tagBit(const char* name) {
  if (!strcmp(name, "pit")) return laptags::PIT;
  if (!strcmp(name, "slow")) return laptags::SLOW;
  if (!strcmp(name, "suspect")) return laptags::GPS_GAP;  // (which kind of suspect doesn't matter here)
  return 0;
}

// Does this file in /sessions hold a session of this course on this day? Names are
// "<YYYYMMDD_hhmmss>_<track id>.json", with "_2", "_3" added if two start in the same second.
inline bool summaryFor(const std::string& fileName, const std::string& trackId, const std::string& event) {
  const size_t slash = fileName.rfind('/');
  const std::string n = slash == std::string::npos ? fileName : fileName.substr(slash + 1);
  if (n.size() < 17 || n.compare(0, 8, event) != 0 || n[8] != '_' || n[15] != '_') return false;
  if (n.size() < 5 || n.compare(n.size() - 5, 5, ".json") != 0) return false;
  const std::string rest = n.substr(16, n.size() - 16 - 5);  // after "YYYYMMDD_hhmmss_", before ".json"
  if (rest == trackId) return true;
  if (rest.size() <= trackId.size() + 1 || rest.compare(0, trackId.size(), trackId) != 0 || rest[trackId.size()] != '_')
    return false;
  for (size_t i = trackId.size() + 1; i < rest.size(); i++)
    if (rest[i] < '0' || rest[i] > '9') return false;
  return true;
}

// The day's session summaries in time order (the names sort by their stamp)
inline std::vector<std::string> daySummaries(std::vector<std::string> names, const std::string& trackId,
                                             const std::string& event) {
  std::vector<std::string> out;
  for (const std::string& n : names)
    if (summaryFor(n, trackId, event)) out.push_back(n);
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
    const size_t sa = a.rfind('/'), sb = b.rfind('/');
    return a.substr(sa == std::string::npos ? 0 : sa + 1) < b.substr(sb == std::string::npos ? 0 : sb + 1);
  });
  return out;
}

// The day's runs so far, in order. Sessions are added one after another; the run being timed now
// is "next".
class Day {
public:
  void clear() { runs_.clear(); }
  void add(const Run& r) { runs_.push_back(r); }
  const std::vector<Run>& runs() const { return runs_; }
  size_t count() const { return runs_.size(); }

  // The number the next run will have
  int nextNumber() const { return (int)runs_.size() + 1; }
  // The fastest run that counts (untagged by pit or suspect), as an index into runs(), or -1.
  // A slow tag alone doesn't rule a run out here: every autocross run is its own try.
  int best() const {
    int b = -1;
    for (size_t i = 0; i < runs_.size(); i++) {
      if (!laptags::clean(runs_[i].tags)) continue;
      if (b < 0 || runs_[i].timeMs < runs_[b].timeMs) b = (int)i;
    }
    return b;
  }
  int last() const { return runs_.empty() ? -1 : (int)runs_.size() - 1; }

private:
  std::vector<Run> runs_;
};

// "Run 4 next"
inline std::string runNextText(int number) { return "Run " + std::to_string(number) + " next"; }

}  // namespace courses

#endif  // COURSES_H
