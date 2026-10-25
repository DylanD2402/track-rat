/**
 * Host-side tests for include/Courses.h: autocross courses as events. Which stages are
 * courses (tracks.json's kind and event, and the old "Course 2026-10-20" names), how a new
 * course is named and numbered, which course is "today's" nearby, which session summaries
 * belong to a course's day, and the day's run list across sessions.
 *
 *   g++ -std=c++17 -Iinclude test/test_courses.cpp -o /tmp/co && /tmp/co
 */

#include <cstdio>
#include "Courses.h"
#include "check.h"

using namespace courses;

static void testMigration() {
  CHECK(eventFromName("Course 2026-10-20") == "20261020", "old name -> date");
  CHECK(eventFromName("Course 2026-10-20 (2)") == "20261020", "old second course of the day");
  CHECK(eventFromName("Course 2026-10-20 (12)") == "20261020", "two digits");
  CHECK(eventFromName("Course 2026-13-20").empty(), "month 13 is not a date");
  CHECK(eventFromName("Course 2026-10-20 extra").empty(), "junk after the date");
  CHECK(eventFromName("Course 2026-10-20 ()").empty(), "empty brackets");
  CHECK(eventFromName("Oct 25 course").empty(), "a new-style name has no date to read");
  CHECK(eventFromName("Course").empty() && eventFromName("").empty(), "short names");
  CHECK(eventFromName("My Course 2026-10-20").empty(), "must start with Course");

  // tracks.json: kind and event as written
  Parsed p = classify(true, "course", "20261025", "Oct 25 course");
  CHECK(p.course && p.event == "20261025", "kind course + event");
  p = classify(true, "course", "", "Course 2026-10-20");
  CHECK(p.course && p.event == "20261020", "kind course, no event: from the name");
  p = classify(true, "course", "garbage", "Renamed");
  CHECK(p.course && p.event.empty(), "kind course, bad event, no date in the name: course with no date");
  p = classify(true, "", "", "Course 2026-10-20");
  CHECK(p.course && p.event == "20261020", "old stage named like a course migrates");
  p = classify(true, "", "", "Sam's hill climb");
  CHECK(!p.course && p.event.empty(), "hill climbs and sprints stay as they are");
  p = classify(true, "kart", "", "Course 2026-10-20");
  CHECK(!p.course, "a kind that isn't course (and isn't absent) is left alone");
  p = classify(false, "course", "20261025", "Oct 25 course");
  CHECK(!p.course, "a track that isn't a stage is never a course");
  printf("  migration: kind/event, \"Course YYYY-MM-DD\" names, hill climbs untouched\n");
}

static void testNaming() {
  CHECK(shortDate("20261025") == "Oct 25", "short date");
  CHECK(shortDate("20260105") == "Jan 5", "no leading zero on the day");
  CHECK(shortDate("20261231") == "Dec 31" && shortDate("") .empty() && shortDate("20261301").empty(), "ends and junk");
  CHECK(dateOfStamp("20261025_141500") == "20261025", "stamp -> date");
  CHECK(dateOfStamp("").empty() && dateOfStamp("SIM_12").empty() && dateOfStamp("2026102").empty(), "no date in the stamp");

  std::vector<std::string> names;
  CHECK(courseName("20261025", names) == "Oct 25 course", "first of the day");
  names.push_back("Oct 25 course");
  CHECK(courseName("20261025", names) == "Oct 25 course 2", "second of the day");
  names.push_back("Oct 25 course 2");
  CHECK(courseName("20261025", names) == "Oct 25 course 3", "third");
  CHECK(courseName("20261026", names) == "Oct 26 course", "another day starts again");
  names.push_back("Oct 25 course 3");
  names.erase(names.begin());  // "Oct 25 course" was renamed on the Wi-Fi page
  CHECK(courseName("20261025", names) == "Oct 25 course", "a free name is reused");
  CHECK(courseName("", {}) == "New course" && courseName("", {"New course"}) == "New course 2", "no date known");

  std::vector<std::string> ids = {"course-20261025", "course-2026-10-20"};
  CHECK(courseId("20261025", ids) == "course-20261025-2", "id taken: numbered");
  CHECK(courseId("20261020", ids) == "course-20261020", "old ids use dashes, new ones don't clash");
  CHECK(courseId("", {}) == "course-new", "no date");
  printf("  naming: \"Oct 25 course\", \"Oct 25 course 2\", ids\n");
}

static void testChoice() {
  const double lat = 45.0, lon = -75.0;
  // 0.01 degrees of latitude is about 1.11 km
  std::vector<Candidate> list = {
      {10, "20261025", lat + 0.01, lon},  // today, 1.1 km
      {11, "20261025", lat + 0.02, lon},  // today, 2.2 km
      {12, "20261020", lat, lon},         // an older course right here
      {13, "20261025", lat + 0.10, lon},  // today but 11 km away
      {14, "", lat, lon + 0.001},         // a course with no date
  };
  CHECK(todaysNearby(list, "20261025", lat, lon) == 0, "today's course: the nearest made today");
  CHECK(todaysNearby(list, "20261026", lat, lon) == -1, "no course made today");
  CHECK(todaysNearby(list, "", lat, lon) == -1, "date not known: none");
  CHECK(todaysNearby({list[3]}, "20261025", lat, lon) == -1, "today's course beyond 5 km is not nearby");
  CHECK(todaysNearby(list, "20261025", lat + 0.2, lon) == -1, "far from all");
  CHECK(forHub(list, "20261025", lat, lon) == 0, "hub: today's first");
  CHECK(forHub(list, "20261030", lat, lon) == 0, "hub: else the most recent within 5 km, the nearer of that date");
  CHECK(forHub({list[2], list[4]}, "20261030", lat, lon) == 0, "hub: a dated course beats one with no date");
  CHECK(forHub({list[4]}, "20261030", lat, lon) == 0, "hub: a course with no date still shows");
  CHECK(forHub({list[3]}, "20261025", lat, lon) == -1, "hub: nothing within 5 km");
  CHECK(forHub({}, "20261025", lat, lon) == -1, "hub: no courses at all");
  std::vector<Candidate> same = {{1, "20261020", lat + 0.02, lon}, {2, "20261020", lat + 0.01, lon}};
  CHECK(forHub(same, "20261025", lat, lon) == 1, "hub: same date, the nearer");
  printf("  choice: today's course nearby, else the most recent\n");
}

static void testSummaries() {
  const std::string id = "course-20261025";
  CHECK(summaryFor("20261025_090000_course-20261025.json", id, "20261025"), "a session of the day");
  CHECK(summaryFor("/sessions/20261025_090000_course-20261025.json", id, "20261025"), "with the folder");
  CHECK(summaryFor("20261025_090000_course-20261025_2.json", id, "20261025"), "same-second suffix");
  CHECK(!summaryFor("20261025_090000_course-20261025-2.json", id, "20261025"), "another course (id -2) is not this one");
  CHECK(!summaryFor("20261025_090000_course-20261025.vbo", id, "20261025"), "the log is not a summary");
  CHECK(!summaryFor("20261025_090000_course-20261025.json.tmp", id, "20261025"), "a temp file is not a summary");
  CHECK(!summaryFor("20261024_090000_course-20261025.json", id, "20261025"), "another day");
  CHECK(!summaryFor("20261025_090000_freeroam.json", id, "20261025"), "another track");
  CHECK(!summaryFor("20261025_090000_course-20261025_x.json", id, "20261025"), "junk suffix");
  CHECK(!summaryFor("20261025_090000_.json", id, "20261025") && !summaryFor("", id, "20261025"), "short names");
  const std::vector<std::string> files = {"/sessions/20261025_140000_course-20261025.json", "/sessions/x.vbo",
                                          "/sessions/20261025_090000_course-20261025.json",
                                          "/sessions/20261025_140000_course-20261025_2.json",
                                          "/sessions/20261025_100000_calabogie.json"};
  const std::vector<std::string> day = daySummaries(files, id, "20261025");
  CHECK(day.size() == 3, "three summaries, got %d", (int)day.size());
  if (day.size() == 3)
    CHECK(day[0].find("090000") != std::string::npos && day[1].find("140000_course-20261025.json") != std::string::npos &&
              day[2].find("_2.json") != std::string::npos,
          "in time order");
  CHECK(tagBit("pit") == laptags::PIT && tagBit("slow") == laptags::SLOW && tagBit("suspect") != 0 && tagBit("x") == 0,
        "tag names");
  printf("  summaries: the day's files for a course, in order\n");
}

static void testRuns() {
  Day d;
  CHECK(d.nextNumber() == 1 && d.best() == -1 && d.last() == -1, "an empty day: Run 1 next");
  // Session 1 (before a power-off): runs 1-3; session 2 (after it): the numbering carries on
  d.add({53400, 0});
  d.add({52310, 0});
  d.add({53020, 0});
  CHECK(d.nextNumber() == 4, "Run 4 next after three");
  CHECK(d.best() == 1 && d.last() == 2, "best is run 2, last is run 3");
  d.add({51000, laptags::PIT});
  CHECK(d.best() == 1, "a pit-tagged run is never the best");
  d.add({50500, laptags::GPS_GAP});
  CHECK(d.best() == 1, "nor a suspect one");
  d.add({54000, laptags::SLOW});
  CHECK(d.nextNumber() == 7 && d.last() == 5, "runs keep counting");
  CHECK(d.best() == 1, "a slow tag isn't faster anyway");
  d.add({49900, laptags::SLOW});
  CHECK(d.best() == 6, "a faster run with only a slow tag still counts");
  d.clear();
  CHECK(d.nextNumber() == 1, "cleared");
  CHECK(runNextText(4) == "Run 4 next", "text");
  printf("  runs: numbered across sessions, best and last\n");
}

int main() {
  printf("Courses tests\n");
  testMigration();
  testNaming();
  testChoice();
  testSummaries();
  testRuns();
  printf(failures ? "%d FAILED\n" : "ALL PASSED\n", failures);
  return failures ? 1 : 0;
}
