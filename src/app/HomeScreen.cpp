#include "HomeScreen.h"

#include "App.h"
#include "AutoStart.h"
#include "Config.h"
#include "CourseRuns.h"
#include "GeoMath.h"
#include "HomeLayout.h"
#include "NextUp.h"
#include "SettingsScreens.h"
#include "TrackDay.h"

namespace HomeScreen {

static AppState statusReturn = AppState::Menu;  // where the GPS Status screen goes back to
static int menuHighlight = 0;  // the home screen's BOOT place (home::Item)
static nextcard::Choice nextUp;     // the home screen's Next up card
static uint32_t nextUpAt = 0;     // millis() it was worked out; 0: out of date
static bool autoPickDone = false;
static bool noTrackNearby = false;
static DriveOffDetector roamDriveOff;  // the menu with no track nearby: driving off starts Free Roam

// Works out the home screen's Next up card: today's autocross course nearby, else a circuit within 5 km,
// else Drive (NextUp.h). The day's runs come from the card a few at a time (CourseRuns.h).
static void refreshNextUp() {
  nextcard::Inputs in;
  in.fix = gps.hasFix();
  if (in.fix) {
    const GpsFix& f = gps.lastFix();
    in.today = todayEvent();
    const std::vector<courses::Candidate> list = courseCandidates();
    const int k = courses::forHub(list, in.today, f.lat, f.lon);
    if (k >= 0) {
      const Track* c = trackDB.get(list[k].index);
      in.courseIndex = list[k].index;
      in.courseEvent = list[k].event;
      in.courseDistM = geo::distanceM(f.lat, f.lon, list[k].lat, list[k].lon);
      in.courseName = std::string(c->displayName().c_str());
      if (in.courseEvent == in.today) {
        CourseRuns::ensure(c->id, c->event);
        in.runsKnown = CourseRuns::ready();
        in.runNext = CourseRuns::day().nextNumber();
        const int best = CourseRuns::day().best();
        in.bestRunMs = best >= 0 ? CourseRuns::day().runs()[best].timeMs : 0;
      }
    }
    double dist = -1;
    const int n = trackDB.findNearest(f.lat, f.lon, TRACK_DETECTION_RADIUS_M, &dist);
    if (n >= 0) {
      const Track* t = trackDB.get(n);
      uint32_t pb = 0, pbDate = 0;
      TrackDay::personalBestFor(n, pb, pbDate);
      in.circuitIndex = n;
      in.circuitDistM = dist;
      in.circuitName = std::string(t->name.c_str());
      in.layoutName = std::string(t->layoutName.c_str());
      in.pbMs = pb;
    }
  }
  nextUp = nextcard::choose(in);
  nextUpAt = millis() | 1;
}

// The Next up card was chosen
static void startNextUp() {
  refreshNextUp();
  switch (nextUp.kind) {
    case nextcard::Kind::Course:  // timing runs on it, armed at the start (as at boot)
      selectedIndex = nextUp.index;
      enterState(AppState::Running);
      break;
    case nextcard::Kind::Circuit:
      selectedIndex = nextUp.index;
      enterState(selectedTrack().stage ? AppState::Running : AppState::TrackConfirm);
      break;
    case nextcard::Kind::Drive:
      enterState(AppState::FreeRoam);
      break;
    case nextcard::Kind::NoFix:
      enterState(AppState::GpsStatus);
      break;
  }
}

static void chooseHomeItem(int item) {
  switch (item) {
    case home::NextUp:
      startNextUp();
      break;
    case home::TrackDay:
      enterState(AppState::TrackSelect);
      break;
    case home::Autocross:
      enterState(AppState::Autocross);
      break;
    case home::Drag:  // Performance: works without a fix too, for the bench demo
      enterState(AppState::Performance);
      break;
    case home::Drive:  // Free Roam needs a fix: without one, the GPS status shows what's missing
      enterState(gps.hasFix() ? AppState::FreeRoam : AppState::GpsStatus);
      break;
    case home::Wifi:
      enterState(AppState::Wifi);
      break;
    case home::Settings:
      SettingsScreens::open();
      break;
  }
  lastRender = 0;
}

// Once the GPS has a fix: a track within range is picked. If the car is
// already moving the session starts on it straight away; if it's parked, the
// track's Ready screen waits for the car to drive off (so the menu is one
// button away). With no track in range, show the menu.
void autoPickTrack() {
  if (autoPickDone || !gps.hasFix()) return;
  if (state != AppState::GpsWait && state != AppState::Menu) return;
  autoPickDone = true;
  double dist;
  // Today's autocross course nearby: timing starts armed for its next run, so a restart between runs needs no taps
  const int course = todaysCourseNearby();
  if (course >= 0) {
    selectedIndex = course;
    DEBUG_PRINTF("Today's course: %s - timing runs, armed at the start\n", selectedTrack().name.c_str());
    enterState(AppState::Running);
    return;
  }
  int nearest = trackDB.findNearest(gps.lastFix().lat, gps.lastFix().lon, TRACK_DETECTION_RADIUS_M, &dist);
  if (nearest >= 0) {
    selectedIndex = nearest;
    if (selectedTrack().stage) {  // a stage: timing from now, so the first run's standing start is caught
      DEBUG_PRINTF("Nearest track: %s (%.0f m) - a stage, timing runs\n", selectedTrack().name.c_str(), dist);
      enterState(AppState::Running);
    } else if (gps.lastFix().speedKmh >= AUTO_START_KMH) {
      DEBUG_PRINTF("Nearest track: %s (%.0f m) - already moving, starting a session\n", selectedTrack().name.c_str(), dist);
      enterState(AppState::Running);
    } else {
      DEBUG_PRINTF("Nearest track: %s (%.0f m) - ready, starts when you drive off\n", selectedTrack().name.c_str(), dist);
      enterState(AppState::TrackConfirm);
      armAutoStart();
    }
  } else {
    noTrackNearby = true;
    DEBUG_PRINTF("No track within %.0f km\n", TRACK_DETECTION_RADIUS_M / 1000.0);
    if (state == AppState::GpsWait) enterState(AppState::Menu);
  }
}

bool awayFromTracks() { return noTrackNearby; }

void forgetNextUp() { nextUpAt = 0; }

void stateChanging(AppState next) {
  if (next == AppState::Menu) roamDriveOff.reset(), nextUpAt = 0;
  if (next == AppState::GpsStatus) statusReturn = state;
}

void onInputGpsWait(InputEvent ev) {
  if (ev != InputEvent::Hold) {
    autoPickDone = true;  // the driver chose to go on without waiting
    enterState(AppState::Menu);
  }
}

void onInputMenu(InputEvent ev) {
  if (ev == InputEvent::TapHeader) enterState(AppState::GpsStatus);
  else if (ev == InputEvent::TapLeft || ev == InputEvent::TapCenter || ev == InputEvent::TapRight)
    chooseHomeItem(home::hit(input.lastTapX(), input.lastTapY()));
  else if (ev == InputEvent::BootShort) menuHighlight = home::next(menuHighlight);
  else if (ev == InputEvent::BootLong) chooseHomeItem(menuHighlight);
  lastRender = 0;
}

void onInputGpsStatus(InputEvent ev) {
  if (ev != InputEvent::Hold) enterState(statusReturn);
}

void renderGpsWait() {
  display.drawGpsWait(gps, trackDB.count(), trackDB.source(), input.touchAvailable(), false);
}

void renderGpsStatus() {
  display.drawGpsWait(gps, trackDB.count(), trackDB.source(), input.touchAvailable(), true);
}

void renderMenu(uint32_t now) {
  if (!nextUpAt || now - nextUpAt >= 500) refreshNextUp();
  display.drawHome(nextUp, input.touchAvailable() ? -1 : menuHighlight, gps, input.touchAvailable());
}

void onFix(const GpsFix& fix) {
  // Away from known tracks, driving off records the drive and watches for a loop
  if (roamDriveOff.onFix(fix.speedKmh, fix.gpsTimeMs)) {
    DEBUG_PRINTLN("Driving off with no track nearby - Free Roam, watching for a loop");
    enterState(AppState::FreeRoam);
  }
}

}  // namespace HomeScreen
