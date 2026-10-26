// Host test for include/NextUp.h and include/HomeLayout.h: the home screen's Next up card and its taps.
//   g++ -std=c++17 -Iinclude test/test_nextup.cpp -o /tmp/nu && /tmp/nu

#include <cstdio>
#include "HomeLayout.h"
#include "NextUp.h"
#include "check.h"

using namespace nextcard;

// A fix, with a course made today 800 m away and a circuit 1.2 km away
static Inputs base() {
  Inputs in;
  in.fix = true;
  in.today = "20261025";
  in.courseIndex = 7;
  in.courseEvent = "20261025";
  in.courseDistM = 800;
  in.courseName = "Oct 25 course";
  in.runNext = 4;
  in.bestRunMs = 52310;
  in.circuitIndex = 3;
  in.circuitDistM = 1200;
  in.circuitName = "Calabogie";
  in.layoutName = "Full";
  in.pbMs = 163611;
  return in;
}

static void testPriorities() {
  Inputs in = base();
  Choice c = choose(in);
  CHECK(c.kind == Kind::Course && c.index == 7, "today's course comes first, ahead of a circuit");
  CHECK(c.title == "Autocross · Oct 25 course", "course title: %s", c.title.c_str());
  CHECK(c.detail == "Run 4 next · best 52.31", "course detail: %s", c.detail.c_str());

  in.courseIndex = -1;
  c = choose(in);
  CHECK(c.kind == Kind::Circuit && c.index == 3, "no course: the circuit");
  CHECK(c.title == "Calabogie · Full", "circuit title: %s", c.title.c_str());
  CHECK(c.detail == "PB 2:43.61 · 1.2 km away", "circuit detail: %s", c.detail.c_str());

  in.circuitIndex = -1;
  c = choose(in);
  CHECK(c.kind == Kind::Drive && c.index == -1, "nothing nearby: Drive");
  CHECK(c.title == "Drive" && c.detail == "Free Roam, it learns tracks", "drive text: %s / %s", c.title.c_str(),
        c.detail.c_str());
  printf("  priorities: course, circuit, drive\n");
}

static void testNoFix() {
  Inputs in = base();
  in.fix = false;
  Choice c = choose(in);
  CHECK(c.kind == Kind::NoFix && c.index == -1, "no fix: wait for GPS, even with tracks known");
  CHECK(c.title == "Waiting for GPS…", "no fix text: %s", c.title.c_str());
  CHECK(!c.detail.empty(), "no fix says what a tap does");
  printf("  no fix\n");
}

static void testCourseRules() {
  Inputs in = base();
  in.courseEvent = "20261024";  // made yesterday
  Choice c = choose(in);
  CHECK(c.kind == Kind::Circuit, "a course made yesterday isn't next up: the circuit is");
  in = base();
  in.today = "";  // the GPS has given no date
  CHECK(choose(in).kind == Kind::Circuit, "no date: no course");
  in = base();
  in.courseDistM = 5000;
  CHECK(choose(in).kind == Kind::Course, "a course exactly 5 km away counts");
  in.courseDistM = 5001;
  CHECK(choose(in).kind == Kind::Circuit, "a course over 5 km away does not");
  in.courseDistM = -1;
  CHECK(choose(in).kind == Kind::Circuit, "unknown distance: not nearby");
  in = base();
  in.bestRunMs = 0;
  in.runNext = 1;
  c = choose(in);
  CHECK(c.detail == "Run 1 next", "no runs yet: %s", c.detail.c_str());
  in.runNext = 12;
  in.bestRunMs = 61230;
  c = choose(in);
  CHECK(c.detail == "Run 12 next · best 1:01.23", "best over a minute: %s", c.detail.c_str());
  in.runNext = 0;
  c = choose(in);
  CHECK(c.detail.compare(0, 10, "Run 1 next") == 0, "run numbers start at 1: %s", c.detail.c_str());
  in.runsKnown = false;  // the card is still being read: no wrong number
  c = choose(in);
  CHECK(c.kind == Kind::Course && c.detail == "Reading the day's runs…", "runs loading: %s", c.detail.c_str());
  printf("  course rules: today only, within 5 km\n");
}

static void testCircuitRules() {
  Inputs in = base();
  in.courseIndex = -1;
  in.circuitDistM = 5000;
  CHECK(choose(in).kind == Kind::Circuit, "a circuit exactly 5 km away counts");
  in.circuitDistM = 5001;
  CHECK(choose(in).kind == Kind::Drive, "a circuit over 5 km away does not");
  in.circuitDistM = -1;
  CHECK(choose(in).kind == Kind::Drive, "unknown distance: not nearby");
  in.circuitDistM = 40;
  in.layoutName = "";
  in.pbMs = 0;
  Choice c = choose(in);
  CHECK(c.title == "Calabogie", "one layout: just the name: %s", c.title.c_str());
  CHECK(c.detail == "No PB yet", "parked at the line, no PB: %s", c.detail.c_str());
  in.pbMs = 59990;
  c = choose(in);
  CHECK(c.detail == "PB 59.99", "PB under a minute: %s", c.detail.c_str());
  in.circuitDistM = 4300;
  in.pbMs = 163611;
  c = choose(in);
  CHECK(c.detail == "PB 2:43.61 · 4.3 km away", "far: %s", c.detail.c_str());
  printf("  circuit rules: within 5 km, PB text\n");
}

static void testLayout() {
  using namespace home;
  CHECK(hit(160, 10) == -1 && hit(0, 29) == -1, "the top bar is the GPS status");
  CHECK(hit(5, 40) == NextUp && hit(315, 100) == NextUp, "the card");
  CHECK(hit(20, 140) == TrackDay && hit(100, 140) == Autocross && hit(200, 140) == Drag && hit(319, 140) == Drive,
        "the four tiles");
  CHECK(hit(79, 140) == TrackDay && hit(80, 140) == Autocross, "tiles split in quarters");
  CHECK(hit(50, 200) == Wifi && hit(250, 200) == Settings && hit(159, 239) == Wifi && hit(160, 239) == Settings,
        "the small row");
  // Everything below the top bar lands somewhere
  for (int y = TOP; y < 240; y++)
    for (int x = 0; x < 320; x += 7) CHECK(hit(x, y) >= 0 && hit(x, y) < ITEM_COUNT, "dead spot at %d,%d", x, y);
  // The drawn bands agree with the tap bands
  CHECK(CARD_Y >= TOP && CARD_Y + CARD_H <= TILE_SPLIT + 3, "card sits in its band");
  CHECK(TILE_Y >= TILE_SPLIT && TILE_Y + TILE_H <= ROW_SPLIT + 3, "tiles sit in their band");
  CHECK(ROW_Y >= ROW_SPLIT && ROW_Y + ROW_H <= 240, "row sits on the screen");
  // BOOT goes through all seven in order and wraps
  const int order[] = {NextUp, TrackDay, Autocross, Drag, Drive, Wifi, Settings};
  int i = NextUp;
  for (int k = 0; k < ITEM_COUNT; k++, i = next(i)) CHECK(i == order[k], "BOOT order, step %d", k);
  CHECK(ITEM_COUNT == 7 && i == NextUp, "BOOT wraps back to Next up");
  printf("  layout: every tap lands on something, BOOT order\n");
}

int main() {
  printf("Next up tests\n");
  testPriorities();
  testNoFix();
  testCourseRules();
  testCircuitRules();
  testLayout();
  printf(failures ? "%d FAILED\n" : "ALL PASSED\n", failures);
  return failures ? 1 : 0;
}
