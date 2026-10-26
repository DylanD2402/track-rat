/**
 * Track Rat - reading the day's logs for the Adjust screen: implementation (see AdjustLoad.h)
 */

#include "AdjustLoad.h"
#include <FS.h>
#include <memory>
#include <string>
#include <vector>
#include "Config.h"
#include "Storage.h"

namespace AdjustLoad {

enum class Step : uint8_t { Idle, Listing, Reading, Done };

static Step step = Step::Idle;
static adjust::Which which = adjust::Which::Finish;
static TrackPoint lineNow = {0, 0, 0, 0, HeadingFrom::None};
static double refLat = 0, refLon = 0;
static std::string trackId, event;
static Storage::DirLister dir;
static File openLog;
static std::vector<std::string> names;  // every .vbo of the day, then sorted
static size_t logPos = 0;
static std::unique_ptr<adjust::DayScan> scan;
static adjust::Model theModel;
static bool failedFlag = false;
static String failedText;

static const uint32_t LIST_BUDGET_MS = 8;  // most of one loop pass given to the folder listing
static const size_t CHUNK = 2048;         // bytes read at a time
static const uint32_t BUDGET_MS = 10;     // most of one loop pass given to the card

adjust::Model& model() { return theModel; }
bool reading() { return step == Step::Listing || step == Step::Reading; }
bool failed() { return failedFlag; }
const String& failure() { return failedText; }
int logsDone() { return step == Step::Reading ? (int)logPos : step == Step::Done ? (int)names.size() : 0; }
int logsTotal() { return step == Step::Reading || step == Step::Done ? (int)names.size() : 0; }

static void fail(const char* why) {
  failedFlag = true;
  failedText = why;
  step = Step::Done;
  dir.close();
  if (openLog) openLog.close();
  scan.reset();
}

void release() {
  dir.close();
  if (openLog) openLog.close();
  scan.reset();
  theModel = adjust::Model();
  std::vector<std::string>().swap(names);
  step = Step::Idle;
  failedFlag = false;
  failedText = "";
}

void start(const Track& course, adjust::Which w) {
  release();
  which = w;
  trackId = course.id.c_str();
  event = course.event.c_str();
  refLat = course.startFinish.lat;
  refLon = course.startFinish.lon;
  lineNow = w == adjust::Which::Start ? course.startFinish : course.finish;

  adjust::Setup s;
  s.which = w;
  s.start = course.startFinish;
  s.hasFinish = course.hasFinish;
  s.finish = course.finish;
  scan.reset(new adjust::DayScan());
  scan->begin(s);

  fs::FS* fs = Storage::sessionFS();
  if (!fs) return fail("Needs the SD card with the session logs");
  if (!courses::validEvent(event)) return fail("This course has no date");
  if (!dir.open(*fs, SESSION_DIR)) return fail("No session logs on the SD card");
  step = Step::Listing;
}

// The model, once every log is read
static void finishUp() {
  dir.close();
  adjust::DayScan& d = *scan;
  if (d.runs().empty()) {
    fail(names.empty() ? "No session logs for this course's day" : "No timed runs in this day's logs");
    return;
  }
  const int kept = (int)d.runs().size();
  theModel.setup(which, lineNow, refLat, refLon, std::move(d.runs()));
  DEBUG_PRINTF("Adjust: %d run(s) kept of %d from %d log(s)\n", kept, d.counted(), d.logs());
  scan.reset();  // the scan's buffers are not needed any more
  step = Step::Done;
}

void poll() {
  if (step == Step::Listing) {
    const uint32_t t0 = millis();
    Storage::DirLister::Entry e;
    do {
      if (!dir.next(e)) {
        names = adjust::logsForDay(names, trackId, event);
        logPos = 0;
        step = Step::Reading;
        return;
      }
      if (!e.isDir && adjust::logFor(e.name, trackId, event)) names.push_back(std::string(SESSION_DIR) + "/" + e.name);
    } while (millis() - t0 < LIST_BUDGET_MS);
  } else if (step == Step::Reading) {
    fs::FS* fs = Storage::sessionFS();
    if (!fs) return fail("The SD card went away");
    const uint32_t t0 = millis();
    do {
      if (!openLog) {
        if (logPos >= names.size()) return finishUp();
        openLog = fs->open(names[logPos].c_str(), "r");
        if (!openLog) {  // a log that won't open: the runs of the others still count
          logPos++;
          continue;
        }
        scan->beginLog();
      }
      static uint8_t chunk[CHUNK];
      const int n = openLog.read(chunk, CHUNK);
      if (n > 0) {
        scan->feed((const char*)chunk, (size_t)n);
      } else {
        openLog.close();
        scan->endLog();
        logPos++;
      }
    } while (millis() - t0 < BUDGET_MS && step == Step::Reading);
  }
}

}  // namespace AdjustLoad
