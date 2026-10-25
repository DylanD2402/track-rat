/**
 * Track Rat - the home screen's "Next up" card (9.1)
 *
 * The big card at the top of the home screen does the most likely thing in
 * one tap. It picks itself, in this order:
 *   1. today's autocross course within 5 km: "Autocross · Oct 25 course",
 *      "Run 4 next · best 52.31" (a tap starts runs on it, armed);
 *   2. a circuit within 5 km: "Calabogie · Full", the PB (a tap goes to its
 *      Ready screen);
 *   3. "Drive": Free Roam, which still learns loops into tracks.
 * With no GPS fix there is no way to tell where we are, so the card waits
 * for one ("Waiting for GPS…") and a tap shows the GPS status.
 *
 * main.cpp finds the nearest course and circuit and fills in Inputs; this
 * decides and words it. Pure C++ (no Arduino); tested in test/test_nextup.cpp.
 */

#ifndef NEXT_UP_H
#define NEXT_UP_H

#include <stdint.h>
#include <stdio.h>
#include <string>
#include "Courses.h"

namespace nextup {

constexpr double NEAR_M = 5000.0;  // "nearby": the same distance as the boot auto-pick

enum class Kind : uint8_t { Course, Circuit, Drive, NoFix };

struct Inputs {
  bool fix = false;
  // The course the Autocross hub would show here (the latest within range): only one made today counts
  int courseIndex = -1;     // the caller's number for it, -1 none
  double courseDistM = -1;  // from here to its start, < 0 unknown
  std::string courseEvent;  // "YYYYMMDD"
  std::string today;        // "YYYYMMDD", "" if the GPS has not given a date
  std::string courseName;   // "Oct 25 course"
  bool runsKnown = true;    // false while the day's runs are still being read from the card
  int runNext = 1;          // the number the next run will have
  uint32_t bestRunMs = 0;   // the day's best run, 0 none yet
  // The nearest circuit (not a course, not the Bench Test)
  int circuitIndex = -1;
  double circuitDistM = -1;
  std::string circuitName;  // "Calabogie"
  std::string layoutName;   // "Full", "" for a track with one layout
  uint32_t pbMs = 0;        // 0 none yet
};

struct Choice {
  Kind kind = Kind::Drive;
  int index = -1;      // Course, Circuit: the caller's number for the track
  std::string title;   // "Autocross · Oct 25 course"
  std::string detail;  // "Run 4 next · best 52.31"
};

// "52.31", or "1:02.31" from a minute (hundredths, cut not rounded, like the clock)
inline std::string timeText(uint32_t ms) {
  char buf[24];
  const unsigned long cs = (ms / 10) % 100, s = (ms / 1000) % 60, m = ms / 60000;
  if (m) snprintf(buf, sizeof(buf), "%lu:%02lu.%02lu", m, s, cs);
  else snprintf(buf, sizeof(buf), "%lu.%02lu", s, cs);
  return buf;
}

// "2.2 km"; "" for under a kilometre (parked at the track, how far is not worth saying)
inline std::string farText(double m) {
  if (m < 1000) return "";
  char buf[24];
  snprintf(buf, sizeof(buf), m < 100000 ? "%.1f km" : "%.0f km", m / 1000.0);
  return buf;
}

inline Choice choose(const Inputs& in) {
  Choice c;
  if (!in.fix) {
    c.kind = Kind::NoFix;
    c.title = "Waiting for GPS…";
    c.detail = "Tap to see the GPS status";
    return c;
  }
  if (in.courseIndex >= 0 && courses::validEvent(in.today) && in.courseEvent == in.today && in.courseDistM >= 0 &&
      in.courseDistM <= NEAR_M) {
    c.kind = Kind::Course;
    c.index = in.courseIndex;
    c.title = "Autocross · " + in.courseName;
    if (!in.runsKnown) {
      c.detail = "Reading the day's runs…";
      return c;
    }
    c.detail = courses::runNextText(in.runNext < 1 ? 1 : in.runNext);
    if (in.bestRunMs) c.detail += " · best " + timeText(in.bestRunMs);
    return c;
  }
  if (in.circuitIndex >= 0 && in.circuitDistM >= 0 && in.circuitDistM <= NEAR_M) {
    c.kind = Kind::Circuit;
    c.index = in.circuitIndex;
    c.title = in.circuitName;
    if (!in.layoutName.empty()) c.title += " · " + in.layoutName;
    c.detail = in.pbMs ? "PB " + timeText(in.pbMs) : std::string("No PB yet");
    const std::string far = farText(in.circuitDistM);
    if (!far.empty()) c.detail += " · " + far + " away";
    return c;
  }
  c.kind = Kind::Drive;
  c.title = "Drive";
  c.detail = "Free Roam, it learns tracks";
  return c;
}

}  // namespace nextup

#endif  // NEXT_UP_H
