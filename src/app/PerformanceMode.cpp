#include "PerformanceMode.h"

#include "App.h"
#include "Config.h"
#include "PerfLog.h"
#include "PerfRun.h"
#include "Settings.h"
#include "Storage.h"

namespace PerformanceMode {

// Performance runs (acceleration and braking)
enum class PerfView { Live, Card, Best };
static perf::PerfRun perfRun;
static PerfView perfView = PerfView::Live;
static int perfCard = 0;            // run shown on the card screen
static bool perfRollout = PERF_ROLLOUT_DEFAULT;
static bool perfDemo = false;       // bench test: a simulated car launches and brakes on repeat
static String perfPath;
static String perfStamp;
static perf::SpeedUnit perfUnits() { return Settings::get().units == SpeedUnits::Mph ? perf::SpeedUnit::Mph : perf::SpeedUnit::Kmh; }

// Bench test for Performance mode: the simulated car launches, reaches 215 km/h, brakes and repeats.
static void startPerfDemo() {
  gps.setSimulated(true, SimMode::Launch);
  perfDemo = true;
  perfRun.clear();
  perfRun.setRollout(perfRollout);
  perfView = PerfView::Live;
  perfPath = "";
  perfStamp = gps.timestampString();
}

static void onPerfResult() {
  const perf::PerfResult& r = perfRun.results().back();
  if (!r.valid) {
    DEBUG_PRINTF("PERF %s: INVALID (GPS gap)\n",
                 r.kind == perf::RunKind::Brake ? "brake" : r.kind == perf::RunKind::Roll ? "roll" : "launch");
  } else if (r.kind == perf::RunKind::Brake) {
    DEBUG_PRINTF("PERF brake 100-0: %.1f m in %.2f s\n", r.brakeM, r.brakeS);
  } else if (r.kind == perf::RunKind::Roll) {
    DEBUG_PRINTF("PERF roll 100-200: %.2f s\n", r.t100to200);
  } else {
    DEBUG_PRINTF("PERF launch: 0-60 %.2f  0-100 %.2f  0-60mph %.2f  100-200 %.2f  1/8 %.2f @ %.0f  1/4 %.2f @ %.0f km/h%s\n",
                 r.t0to60, r.t0to100, r.t0to60mph, r.t100to200, r.et8, r.trap8Kmh, r.et4, r.trap4Kmh,
                 r.notLevel ? "  NOT LEVEL" : "");
  }
  if (perfPath.length() == 0) perfPath = PerfLog::newPath(perfStamp);
  if (PerfLog::save(perfPath, perfStamp, perfRun)) {
    DEBUG_PRINTF("Saved %s:%s\n", Storage::sessionFSName(), perfPath.c_str());
  }
  perfCard = (int)perfRun.results().size() - 1;
  perfView = PerfView::Card;
}

void enter() {
  perfRun.clear();
  perfRun.setRollout(perfRollout);
  perfView = PerfView::Live;
  perfCard = 0;
  perfDemo = false;
  perfPath = "";
  perfStamp = gps.timestampString();
  DEBUG_PRINTLN("Performance mode started");
}

void leave() {
  if (perfDemo) gps.setSimulated(false);
  perfDemo = false;
}

void input(InputEvent ev) {
  // Taps are ignored while the car is moving, so a bump can't leave a run. (No fix: the speed is stale.)
  const bool busy = gps.hasFix() && (perfRun.state() == perf::PerfRun::State::Go || perfRun.speedKmh() > 5.0f);
  const int runs = (int)perfRun.results().size();
  if (ev == InputEvent::Hold || ev == InputEvent::BootLong) {
    enterState(AppState::Menu);
  } else if (ev == InputEvent::BootShort) {  // Live -> latest run -> session best -> Live
    if (perfView == PerfView::Live && !gps.hasFix() && !perfDemo) startPerfDemo();
    else if (perfView == PerfView::Live) perfView = runs ? PerfView::Card : PerfView::Best;
    else if (perfView == PerfView::Card) perfView = PerfView::Best;
    else perfView = PerfView::Live;
    if (perfView == PerfView::Card) perfCard = runs - 1;
  } else if (!busy && (ev == InputEvent::TapLeft || ev == InputEvent::TapCenter || ev == InputEvent::TapRight)) {
    if (perfView == PerfView::Live) {
      if (ev == InputEvent::TapLeft) {
        if (!gps.hasFix() && !perfDemo) {
          startPerfDemo();
        } else {
          perfRollout = !perfRollout;
          perfRun.setRollout(perfRollout);
        }
      } else if (ev == InputEvent::TapCenter) {
        perfView = runs ? PerfView::Card : PerfView::Best;
        perfCard = runs - 1;
      } else {
        enterState(AppState::Menu);
      }
    } else if (perfView == PerfView::Card) {
      if (ev == InputEvent::TapLeft && runs) perfCard = (perfCard + runs - 1) % runs;
      else if (ev == InputEvent::TapCenter) perfView = PerfView::Best;
      else if (ev == InputEvent::TapRight) perfView = PerfView::Live;
    } else {
      if (ev == InputEvent::TapLeft && runs) perfView = PerfView::Card, perfCard = runs - 1;
      else if (ev == InputEvent::TapRight) perfView = PerfView::Live;
    }
  }
  lastRender = 0;
}

void render() {
  if (perfRun.state() == perf::PerfRun::State::Go) perfView = PerfView::Live;  // a new run is on
  if (perfView == PerfView::Live) {
    const char* left = (!gps.hasFix() && !perfDemo) ? "Demo" : "1 ft";
    display.drawPerfLive(perfRun, gps, perfUnits(), left, ::input.touchHeldMs(), ::input.touchAvailable());
  } else {
    display.drawPerfCard(perfRun, perfView == PerfView::Card ? perfCard : -1, perfUnits(), ::input.touchAvailable());
  }
}

void onFix(const GpsFix& fix) {
  if (perfRun.onFix(fix.gpsTimeMs, fix.speedKmh, fix.altM) > 0) onPerfResult();
}

bool still() { return perfView != PerfView::Live; }

}  // namespace PerformanceMode
