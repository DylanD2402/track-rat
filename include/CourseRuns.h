/**
 * Track Rat - the day's runs at a course, from the session summaries (9.2)
 *
 * Runs at a course are numbered across every session of its event day, so
 * "Run 4 next" is right after a power-off or a session ending. The earlier
 * sessions' runs are read from their summaries in /sessions (never the
 * logs): a few directory entries or one summary each time poll() is called
 * from loop(), so the screen never waits for the card. A session that is
 * running now isn't read (its own file is skipped): its runs come from the
 * lap timer.
 *
 * The list is kept in memory. start() reads it again; main.cpp does that
 * when the Autocross hub opens, when a course session starts (only if a
 * session ended or another course is wanted since), and so on.
 */

#ifndef COURSE_RUNS_H
#define COURSE_RUNS_H

#include <Arduino.h>
#include "Courses.h"

namespace CourseRuns {

// Begin reading the runs of `trackId` on `event` (YYYYMMDD) from the summaries on the card.
// skipPath: a summary to leave out (the running session's), or "". Without a card the list stays empty.
void start(const String& trackId, const String& event, const String& skipPath = "");
// The running session's own summary, if its file turns up while the list is being read: left out
void setSkip(const String& path);
// A little more of the work; cheap when there is none. Call every loop().
void poll();
bool ready();  // the list is complete (also true with no card)
bool wanted(const String& trackId, const String& event);  // start() was called for this course and day
// A session at the course ended or changed: the list is out of date. The next ensure() reads it again.
void markStale();
// start() unless the list is already for this course and day and still fresh
void ensure(const String& trackId, const String& event, const String& skipPath = "");

const courses::Day& day();  // the runs read so far

}  // namespace CourseRuns

#endif  // COURSE_RUNS_H
