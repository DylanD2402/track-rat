/**
 * Track Rat - the day's runs at a course: implementation (see CourseRuns.h)
 */

#include "CourseRuns.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <string>
#include <vector>
#include "Config.h"
#include "PsramAlloc.h"
#include "Storage.h"

namespace CourseRuns {

enum class Step : uint8_t { Idle, Listing, Reading, Done };

static Step step = Step::Done;
static String wantId, wantEvent, skip;
static bool stale = true;
static courses::Day runs;
static Storage::DirLister dir;
static std::vector<std::string> files;  // matching summaries (full paths)
static size_t readPos = 0;

static const uint32_t LIST_BUDGET_MS = 8;  // most of one loop pass given to the folder listing

const courses::Day& day() { return runs; }
bool ready() { return step == Step::Done; }
bool wanted(const String& trackId, const String& event) { return trackId == wantId && event == wantEvent; }
void markStale() { stale = true; }
void setSkip(const String& path) { skip = path; }

void start(const String& trackId, const String& event, const String& skipPath) {
  dir.close();
  wantId = trackId;
  wantEvent = event;
  skip = skipPath;
  runs.clear();
  files.clear();
  readPos = 0;
  stale = false;
  fs::FS* fs = Storage::sessionFS();
  if (!fs || trackId.length() == 0 || !courses::validEvent(event.c_str())) {
    step = Step::Done;
    return;
  }
  if (!dir.open(*fs, SESSION_DIR)) {
    step = Step::Done;
    return;
  }
  step = Step::Listing;
}

void ensure(const String& trackId, const String& event, const String& skipPath) {
  if (stale || !wanted(trackId, event)) start(trackId, event, skipPath);
}

// One summary's runs, in order: "laps": [{"timeMs": 52310, "tags": ["pit"]}, ...]
static void readSummary(fs::FS& fs, const std::string& path) {
  File f = fs.open(path.c_str(), "r");
  if (!f) return;
  StaticJsonDocument<96> filter;
  filter["laps"][0]["timeMs"] = true;
  filter["laps"][0]["tags"] = true;
  PsramJsonDocument doc(12288);
  const DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filter));
  f.close();
  if (err && err != DeserializationError::NoMemory) return;  // a summary cut off: use what was read
  for (JsonObject lap : doc["laps"].as<JsonArray>()) {
    courses::Run r;
    r.timeMs = lap["timeMs"] | 0u;
    if (r.timeMs == 0) continue;
    for (JsonVariant t : lap["tags"].as<JsonArray>()) r.tags |= courses::tagBit(t.as<const char*>() ? t.as<const char*>() : "");
    runs.add(r);
  }
}

void poll() {
  if (step == Step::Listing) {
    const uint32_t t0 = millis();
    Storage::DirLister::Entry e;
    do {
      if (!dir.next(e)) {
        // The names sort by their time stamp: runs go in the order they were driven
        files = courses::daySummaries(files, wantId.c_str(), wantEvent.c_str());
        readPos = 0;
        step = Step::Reading;
        return;
      }
      if (!e.isDir && courses::summaryFor(e.name, wantId.c_str(), wantEvent.c_str())) {
        const std::string path = std::string(SESSION_DIR) + "/" + e.name;
        if (!skip.length() || skip != path.c_str()) files.push_back(path);
      }
    } while (millis() - t0 < LIST_BUDGET_MS);
  } else if (step == Step::Reading) {
    fs::FS* fs = Storage::sessionFS();
    if (fs && readPos < files.size()) {
      const std::string& path = files[readPos++];
      if (!skip.length() || skip != path.c_str()) readSummary(*fs, path);
    }
    if (!fs || readPos >= files.size()) {
      step = Step::Done;
      files.clear();
      files.shrink_to_fit();
      DEBUG_PRINTF("Course runs: %d earlier run(s) on %s\n", (int)runs.count(), wantEvent.c_str());
    }
  }
}

}  // namespace CourseRuns
