#include "AutocrossScreens.h"

#include "App.h"
#include "AdjustLoad.h"
#include "AutocrossLayout.h"
#include "Config.h"
#include "CourseRuns.h"
#include "Courses.h"
#include "SessionLog.h"
#include "Storage.h"

namespace AutocrossScreens {

// The Autocross hub (Courses.h): which course it shows (a trackDB index, -1 none), and the BOOT button's place:
// 0 Start runs, 1 New course, 2 Adjust start, 3 Adjust finish, 4 Back
static int hubCourse = -1;
static int autocrossHighlight = 0;
static int newCourseHighlight = 1;  // the New course screen: 0 Back, 1 Set start here
// Adjust start / Adjust finish (functions further down, with the other hub code)
static adjust::Which adjustWhich = adjust::Which::Finish;
static int adjustHighlight = adjust::BTN_LATER;
static adjust::View adjustView;
static bool adjustSaving = false;    // Save pressed: the line is written, the runs are being timed again
static bool adjustRetiming = false;  // the re-time task was started for this (until it ends)
static String adjustSaveError;       // the line is saved but the runs could not be timed again

// The Autocross hub's New course, then Set start here: a course (a stage made for today, Courses.h)
// starting where the car stands, timing runs from the next movement. The finish is tapped on the
// first run, with a few seconds to take it back.
static bool courseStartReady() { return gps.hasFix() && gps.lastFix().speedKmh < STAGE_STOPPED_KMH; }

static void createNewCourse() {
  if (!courseStartReady()) return;
  const GpsFix& f = gps.lastFix();
  int idx = trackDB.createCourse(f.lat, f.lon, gps.timestampString());
  if (idx < 0) return enterState(AppState::Autocross);
  selectedIndex = idx;
  enterState(AppState::Running);
  sessionKeepLayout();
  sessionNotice("Start set: the clock starts as you go", 10000);
}

// --- Adjust start / Adjust finish (AdjustLine.h, AdjustLoad.h): the Autocross hub's course, its line moved in
// 0.5 m steps with the day's runs re-timed live. BOOT: hold picks the button (Later, Cancel, Save, Earlier, Reset,
// round again), press uses it. Touch: tap, or hold an arrow to repeat. Save writes the line to tracks.json and times the
// course's logs again (SessionLog::retime, as the Wi-Fi page's Re-time does).
static uint32_t adjustRepeatAt = 0;
static const uint32_t ADJUST_REPEAT_DELAY_MS = 400;  // held this long, an arrow repeats

// Can the hub's Adjust start / Adjust finish be used (adjust::Block::None), and if not, why
static adjust::Block adjustBlockFor(adjust::Which w) {
  const bool card = Storage::sessionFS() != nullptr;
  if (hubCourse < 0) return adjust::block(w, true, true, 0, false, card, SessionLog::retiming());  // no course: needs a run
  const Track& c = *trackDB.get(hubCourse);
  const bool have = CourseRuns::wanted(c.id, c.event);
  return adjust::block(w, c.hasStartFinish, c.hasFinish, have ? (int)CourseRuns::day().count() : 0,
                       !have || !CourseRuns::ready(), card, SessionLog::retiming());
}

static void openAdjust(adjust::Which w) {
  if (adjustBlockFor(w) != adjust::Block::None) return;
  adjustWhich = w;
  enterState(AppState::AdjustLine);
}

// Save: the moved line into tracks.json, then the course's logs timed again; the hub follows when that is done
static void adjustSave() {
  flushSummary();  // (nothing waits here: the session's summary was finished when it ended)
  adjust::Model& m = AdjustLoad::model();
  if (m.offsetM() == 0 || hubCourse < 0) return enterState(AppState::Autocross);  // nothing moved: nothing to save
  const TrackPoint p = m.moved();
  const float heading = p.hasHeading() ? p.heading : -1.0f;
  if (adjustWhich == adjust::Which::Start) trackDB.setStartFinish(hubCourse, p.lat, p.lon, heading);
  else trackDB.setFinish(hubCourse, p.lat, p.lon, heading);
  CourseRuns::markStale();
  String error;
  adjustRetiming = SessionLog::retime(*trackDB.get(hubCourse), "", error);
  adjustSaveError = adjustRetiming ? String() : error;
  adjustSaving = true;
  DEBUG_PRINTF("Adjust: %s moved %+.1f m, %s\n", adjustWhich == adjust::Which::Start ? "start" : "finish", m.offsetM(),
               adjustRetiming ? "re-timing" : error.c_str());
}

// The re-time task ended: the day's runs in the summaries are right again
static void adjustRetimeTick() {
  if (!adjustRetiming || SessionLog::retiming()) return;
  adjustRetiming = false;
  CourseRuns::markStale();
  if (state == AppState::AdjustLine) enterState(AppState::Autocross);  // (reads the runs again)
  else if (state == AppState::Autocross && hubCourse >= 0)
    CourseRuns::start(trackDB.get(hubCourse)->id, trackDB.get(hubCourse)->event);
}

static bool adjustReady() { return !adjustSaving && !AdjustLoad::reading() && !AdjustLoad::failed(); }

static void adjustPress(int button) {
  adjust::Model& m = AdjustLoad::model();
  switch (button) {
    case adjust::BTN_EARLIER: m.nudge(-1); break;
    case adjust::BTN_LATER: m.nudge(1); break;
    case adjust::BTN_RESET: m.reset(); break;
    case adjust::BTN_CANCEL: enterState(AppState::Autocross); break;
    case adjust::BTN_SAVE: adjustSave(); break;
  }
}

static void adjustInput(InputEvent ev) {
  const bool tap = ev == InputEvent::TapLeft || ev == InputEvent::TapCenter || ev == InputEvent::TapRight;
  if (!adjustReady()) {  // reading, nothing to adjust, or saving: only a way back
    if (ev == InputEvent::TapHeader || ev == InputEvent::BootShort || ev == InputEvent::BootLong || (tap && input.lastTapY() >= adjust::ROW_B_Y - 1))
      enterState(AppState::Autocross);
    return;
  }
  if (tap) {
    const int b = adjust::buttonAt(input.lastTapX(), input.lastTapY());
    // A long press on an arrow was already repeated while it was held
    const bool arrow = b == adjust::BTN_EARLIER || b == adjust::BTN_LATER;
    if (b >= 0 && !(arrow && input.lastTapHeldMs() >= ADJUST_REPEAT_DELAY_MS)) adjustPress(b);
  } else if (ev == InputEvent::TapHeader) {
    enterState(AppState::Autocross);  // Cancel, as the top bar goes back elsewhere
  } else if (ev == InputEvent::BootShort) {
    adjustPress(adjustHighlight);
  } else if (ev == InputEvent::BootLong) {
    adjustHighlight = (adjustHighlight + 1) % adjust::BTN_COUNT;
  }  // (a touch held for 2 s, Hold, does nothing here: an arrow being held goes on)
}

// Each loop: an arrow held on the screen repeats, faster the longer it is held
static void adjustHeld(uint32_t now) {
  if (state != AppState::AdjustLine || !adjustReady() || !input.touchAvailable()) return;
  const uint32_t held = input.touchDownMs();
  if (held < ADJUST_REPEAT_DELAY_MS) return;
  const int b = adjust::buttonAt(input.touchX(), input.touchY());
  if ((b != adjust::BTN_EARLIER && b != adjust::BTN_LATER) || now - adjustRepeatAt < (held < 2000 ? 150u : 70u)) return;
  adjustRepeatAt = now;
  AdjustLoad::model().nudge((b == adjust::BTN_LATER ? 1 : -1) * (held < 2000 ? 1 : 2));
  lastRender = 0;
}

// What to draw now
static const adjust::View& adjustViewNow() {
  adjust::View& v = adjustView;
  if (adjustSaving) {
    const SessionLog::RetimeStatus st = SessionLog::retimeStatus();
    v.finish = adjustWhich == adjust::Which::Finish;
    v.phase = adjustSaveError.length() ? adjust::Phase::SaveFailed : adjust::Phase::Saving;
    v.done = st.done;
    v.total = st.total;
    v.message = adjustSaveError.length() ? std::string(adjustSaveError.c_str()) + ". Re-time it on the Wi-Fi page."
                                         : std::string("The line is saved. Timing the day's sessions again.");
  } else if (AdjustLoad::failed()) {
    v.finish = adjustWhich == adjust::Which::Finish;
    v.phase = adjust::Phase::NoLogs;
    v.message = AdjustLoad::failure().c_str();
  } else if (AdjustLoad::reading()) {
    v.finish = adjustWhich == adjust::Which::Finish;
    v.phase = adjust::Phase::Reading;
    v.done = AdjustLoad::logsDone();
    v.total = AdjustLoad::logsTotal();
    v.message = v.total ? "Log " + std::to_string(v.done + 1) + " of " + std::to_string(v.total) + ". Only the fixes near the line are kept."
                        : std::string("Looking for the day's logs.");
  } else {
    AdjustLoad::model().fillView(v);
  }
  return v;
}

void stateChanging(AppState next) {
  // The Autocross hub: today's course nearby, else the latest one within 5 km, and its day's runs from the card
  if (next == AppState::Autocross && state != AppState::NewCourse) {
    hubCourse = -1;
    if (gps.hasFix()) {
      const std::vector<courses::Candidate> list = courseCandidates();
      const int k = courses::forHub(list, todayEvent(), gps.lastFix().lat, gps.lastFix().lon);
      if (k >= 0) hubCourse = list[k].index;
    }
    autocrossHighlight = hubCourse >= 0 ? 0 : 1;
    if (hubCourse >= 0) CourseRuns::start(trackDB.get(hubCourse)->id, trackDB.get(hubCourse)->event);
  }
  if (next == AppState::NewCourse) newCourseHighlight = 1;
  // Adjust start / finish: the day's logs are read from the card while the screen is up, and let go on leaving it
  if (next == AppState::AdjustLine && hubCourse >= 0) {
    adjustView = adjust::View();
    adjustHighlight = adjust::BTN_LATER;
    adjustSaving = false;
    adjustSaveError = "";
    AdjustLoad::start(*trackDB.get(hubCourse), adjustWhich);
  } else if (state == AppState::AdjustLine && next != AppState::AdjustLine) {
    AdjustLoad::release();
    adjustView = adjust::View();
  }
}

void onInputHub(InputEvent ev) {
  const bool canStart = hubCourse >= 0, canMake = gps.hasFix();
  int choice = -1;  // 0 Start runs, 1 New course, 2 Adjust start, 3 Adjust finish, 4 Back
  if (ev == InputEvent::TapHeader || ev == InputEvent::Hold) choice = 4;
  else if (ev == InputEvent::TapLeft || ev == InputEvent::TapCenter || ev == InputEvent::TapRight)
    choice = autocross_layout::buttonAt(input.lastTapX(), input.lastTapY());  // the rectangles the screen draws (AutocrossLayout.h)
  else if (ev == InputEvent::BootShort) {  // the buttons that work
    for (int step = 1; step <= 5; step++) {
      const int next = (autocrossHighlight + step) % 5;
      if ((next == 0 && canStart) || (next == 1 && canMake) || next == 4 ||
          (next == 2 && adjustBlockFor(adjust::Which::Start) == adjust::Block::None) ||
          (next == 3 && adjustBlockFor(adjust::Which::Finish) == adjust::Block::None)) {
        autocrossHighlight = next;
        break;
      }
    }
  } else if (ev == InputEvent::BootLong) choice = autocrossHighlight;
  if (choice == 0 && canStart) {
    selectedIndex = hubCourse;
    enterState(AppState::Running);
  } else if (choice == 1 && canMake) {
    enterState(AppState::NewCourse);
  } else if (choice == 2 || choice == 3) {
    openAdjust(choice == 2 ? adjust::Which::Start : adjust::Which::Finish);
  } else if (choice == 4) {
    enterState(AppState::Menu);
  }
  lastRender = 0;
}

void onInputAdjust(InputEvent ev) {
  adjustInput(ev);
  lastRender = 0;
}

void onInputNewCourse(InputEvent ev) {
  if (ev == InputEvent::TapLeft || ev == InputEvent::TapHeader || ev == InputEvent::Hold) enterState(AppState::Autocross);
  else if (ev == InputEvent::TapCenter || ev == InputEvent::TapRight) createNewCourse();
  else if (ev == InputEvent::BootShort) newCourseHighlight ^= 1;
  else if (ev == InputEvent::BootLong) {
    if (newCourseHighlight == 1) createNewCourse();
    else enterState(AppState::Autocross);
  }
  lastRender = 0;
}

void renderHub() {
  AutocrossView v;
  v.gpsFix = gps.hasFix();
  if (hubCourse >= 0) {
    const Track& c = *trackDB.get(hubCourse);
    v.haveCourse = true;
    v.name = c.displayName();
    v.today = c.event.length() && c.event == todayEvent().c_str();
    if (!v.today && c.event.length()) v.dateText = courses::shortDate(std::string(c.event.c_str())).c_str();
    if (CourseRuns::wanted(c.id, c.event)) {
      v.runs = CourseRuns::day().runs();
      v.loading = !CourseRuns::ready();
    }
    v.nextRun = (int)v.runs.size() + 1;
    v.best = CourseRuns::day().best();
    v.last = CourseRuns::day().last();
  }
  v.retiming = SessionLog::retiming();
  v.adjustReason[0] = adjust::blockText(adjustBlockFor(adjust::Which::Start));
  v.adjustReason[1] = adjust::blockText(adjustBlockFor(adjust::Which::Finish));
  display.drawAutocross(v, gps, input.touchAvailable() ? -1 : autocrossHighlight, input.touchAvailable());
}

void renderAdjust() {
  display.drawAdjust(adjustViewNow(), input.touchAvailable() ? -1 : adjustHighlight, input.touchAvailable());
}

void renderNewCourse() {
  display.drawNewCourse(gps, courseStartReady(), input.touchAvailable() ? -1 : newCourseHighlight, input.touchAvailable());
}

void poll(uint32_t now) {
  if (state == AppState::AdjustLine) AdjustLoad::poll();  // the day's logs, a little at a time (this is not the timing screen)
  adjustRetimeTick();
  adjustHeld(now);
}

}  // namespace AutocrossScreens
