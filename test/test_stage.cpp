/**
 * Host-side tests for point to point timing (LapTimer::setStage): autocross
 * runs with a standing start (armed at the start, the clock starts at the
 * first movement), a start line a little way on, a rolling start, the
 * finish, splits, the live delta to the best run, a run with no finish set
 * (ends when the car stops, or at a tap), and a run that stops short.
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_stage.cpp src/LapTimer.cpp -o /tmp/stg && /tmp/stg
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "LapTimer.h"
#include "check.h"

static const double REF_LAT = 45.0, REF_LON = -75.0;

static TrackPoint at(double x, double y, float heading = -1) {
  TrackPoint p = {0, 0, 10.0f, heading < 0 ? 0 : heading, heading < 0 ? HeadingFrom::None : HeadingFrom::Set};
  geo::fromLocal(x, y, REF_LAT, REF_LON, p.lat, p.lon);
  return p;
}

// A car, fed to the timer a fix every 50 ms (20 Hz)
struct Car {
  LapTimer& timer;
  uint32_t ms = 36000000;  // 10:00 UTC
  double x = 0, y = 0;
  std::vector<LapEvent> events;
  int32_t lastDelta = NO_DELTA;
  uint32_t stoppedUntilMs = 0;  // time of the last fix at a standstill

  explicit Car(LapTimer& t) : timer(t) {}

  void fix(float kmh) {
    double lat, lon;
    geo::fromLocal(x, y, REF_LAT, REF_LON, lat, lon);
    for (const LapEvent& e : timer.onFix(lat, lon, kmh, ms, ms, -1)) events.push_back(e);
    if (timer.liveDeltaMs() != NO_DELTA) lastDelta = timer.liveDeltaMs();
    if (kmh < 1) stoppedUntilMs = ms;
    ms += 50;
  }
  void wait(double seconds) {
    for (int i = 0; i < seconds * 20; i++) fix(0);
  }
  // Straight to (tx, ty): from a standstill (accelerating at `accel` m/s^2) or rolling, at up to vmax km/h
  void driveTo(double tx, double ty, float vmaxKmh, float startKmh = 0, double accel = 6) {
    double v = startKmh / 3.6;
    const double vmax = vmaxKmh / 3.6;
    const double len = hypot(tx - x, ty - y), ux = (tx - x) / len, uy = (ty - y) / len;
    double done = 0;
    while (done < len) {
      v = fmin(vmax, v + accel * 0.05);
      const double step = fmin(v * 0.05, len - done);
      done += step;
      x += ux * step;
      y += uy * step;
      fix((float)(v * 3.6));
    }
  }
  // Brakes to a stop over `d` metres onwards
  void stopWithin(double dx, double dy, float fromKmh) {
    double v = fromKmh / 3.6;
    const double len = hypot(dx, dy), ux = dx / len, uy = dy / len;
    const double decel = v * v / (2 * len);
    while (v > 0.05) {
      v = fmax(0, v - decel * 0.05);
      x += ux * v * 0.05;
      y += uy * v * 0.05;
      fix((float)(v * 3.6));
    }
    fix(0);
  }
  int count(LapEventType type) const {
    int n = 0;
    for (const LapEvent& e : events) n += e.type == type;
    return n;
  }
};

// The course: start at (0, 0), east 300 m, north 200 m to the finish at (300, 200). Then on to stop.
static void run(Car& c, float vmax) {
  c.driveTo(300, 0, vmax);
  c.driveTo(300, 200, vmax, vmax);
  c.driveTo(300, 240, vmax, vmax);
  c.stopWithin(0, 30, vmax);
}
// Back to the start the other way round (a return road 40 m west of the course)
static void back(Car& c) {
  c.driveTo(260, c.y, 30, 0);
  c.driveTo(260, -40, 30, 30);
  c.driveTo(-30, -40, 30, 30);
  c.driveTo(-30, 0, 30, 30);
  c.driveTo(0, 0, 20, 20);
  c.stopWithin(0.5, 0, 20);
}

int main() {
  printf("Point to point tests\n");

  // Autocross: standing start with no start direction, a finish line with one, a split
  {
    LapTimer timer;
    timer.begin(at(0, 0), {at(300, 0)}, MIN_LAP_TIME_MS);
    timer.setStage(at(300, 200, 0), true);
    CHECK(timer.isStage() && timer.hasFinish(), "stage set up");
    Car c(timer);
    c.wait(2);  // pulling up: the timer hasn't seen the start yet... it has: parked on it
    CHECK(timer.armed(), "stopped at the start: armed");
    c.wait(5);
    const uint32_t goMs = c.ms - 50;  // the last fix stopped
    run(c, 80);
    CHECK(!timer.armed(), "not armed once gone");
    CHECK(timer.lapCount() == 1, "run 1 timed: %d", (int)timer.lapCount());
    CHECK(!timer.lapRunning(), "no lap after the finish");
    CHECK(c.count(LapEventType::SplitCrossed) == 1, "split crossed");
    const uint32_t run1 = timer.lapCount() ? timer.lap(0).timeMs : 0;
    // By hand: 300 m + 200 m from a standstill at 6 m/s^2 to 80 km/h
    const double v = 80 / 3.6, accelT = v / 6, accelD = v * v / 12;
    const double expect = (accelT + (500 - accelD) / v) * 1000;
    CHECK(fabs(run1 - expect) < 150, "run 1 %lu ms, about %.0f", (unsigned long)run1, expect);
    CHECK(timer.lap(0).splitMs.size() == 1 && timer.lap(0).splitMs[0] > 0, "split time");
    (void)goMs;

    back(c);
    c.wait(10);  // waiting in line
    CHECK(timer.armed(), "back at the start: armed for run 2");
    CHECK(timer.lapCount() == 1, "driving back isn't a run: %d", (int)timer.lapCount());
    c.lastDelta = NO_DELTA;
    run(c, 90);
    CHECK(timer.lapCount() == 2, "run 2: %d", (int)timer.lapCount());
    CHECK(timer.lapCount() == 2 && timer.lap(1).timeMs < run1, "run 2 quicker");
    CHECK(timer.bestLapIndex() == 1, "best run is run 2");
    CHECK(c.lastDelta != NO_DELTA && c.lastDelta < 0, "live delta to run 1, gaining: %ld", (long)c.lastDelta);
    CHECK(c.count(LapEventType::RunEnded) == 0, "no run ended early");
  }

  // Timing lights a little way on: stopped 10 m behind a start line facing east, the line starts the clock
  {
    LapTimer timer;
    timer.begin(at(0, 0, 90), {}, MIN_LAP_TIME_MS);
    timer.setStage(at(300, 200, 0), true);
    Car c(timer);
    c.x = -10;
    c.wait(3);
    CHECK(timer.armed(), "armed behind the line");
    c.driveTo(-5, 0, 60);
    CHECK(!timer.lapRunning(), "moving off behind the line doesn't start the clock");
    c.driveTo(0, 0, 60, (float)fmin(60, sqrt(2 * 6 * 5.0) * 3.6));
    c.driveTo(300, 0, 60, 60);  // (on from 0 m)
    CHECK(timer.lapRunning() || timer.lapCount(), "the line started it");
    c.driveTo(300, 200, 60, 60);
    c.driveTo(300, 220, 60, 60);
    CHECK(timer.lapCount() == 1, "timed from the line: %d", (int)timer.lapCount());
    // From the line at about 30 km/h (after 10 m from rest) up to 60: about 500 m at 60, less than a standing start
    if (timer.lapCount()) CHECK(timer.lap(0).timeMs < 32000 && timer.lap(0).timeMs > 29000, "run %lu ms",
                                (unsigned long)timer.lap(0).timeMs);
  }

  // A rolling start (a sprint): through the start line at speed, never stopped there
  {
    LapTimer timer;
    timer.begin(at(0, 0, 90), {}, MIN_LAP_TIME_MS);
    timer.setStage(at(300, 200, 0), true);
    Car c(timer);
    c.x = -200;
    c.driveTo(-100, 0, 80);
    c.driveTo(300, 0, 80, 80);
    c.driveTo(300, 210, 80, 80);
    CHECK(timer.lapCount() == 1, "rolling start timed: %d", (int)timer.lapCount());
    if (timer.lapCount()) CHECK(fabs(timer.lap(0).timeMs - 500 / (80 / 3.6) * 1000) < 120, "at 80 km/h all the way: %lu",
                                (unsigned long)timer.lap(0).timeMs);
  }

  // No finish yet: the run ends when the car stops; next time a tap at the finish times it
  {
    LapTimer timer;
    timer.begin(at(0, 0), {}, MIN_LAP_TIME_MS);
    TrackPoint none = {0, 0, 0};
    timer.setStage(none, false);
    Car c(timer);
    c.wait(3);
    run(c, 70);
    CHECK(timer.lapRunning(), "still running just after stopping");
    c.wait(STAGE_END_STOP_MS / 1000.0 + 0.5);
    CHECK(!timer.lapRunning() && timer.lapCount() == 0, "stopped: the run ends, untimed");
    CHECK(c.count(LapEventType::RunEnded) == 1, "RunEnded");
    back(c);
    c.wait(3);
    c.driveTo(300, 0, 70);
    c.driveTo(300, 200, 70, 70);
    double lat, lon;
    geo::fromLocal(c.x, c.y, REF_LAT, REF_LON, lat, lon);
    LapEvents evs = timer.finishRunNow(lat, lon, c.ms - 50);
    CHECK(evs.find(LapEventType::LapCompleted) && timer.lapCount() == 1, "tapped at the finish: timed");
    CHECK(!timer.lapRunning(), "and over");
    // Then the finish is set there (main.cpp saves it) and the next run finishes by itself
    timer.setStage(at(300, 200, 0), true);
    c.driveTo(300, 240, 70, 70);
    c.stopWithin(0, 20, 70);
    back(c);
    c.wait(3);
    run(c, 75);
    CHECK(timer.lapCount() == 2, "next run finishes at the line: %d", (int)timer.lapCount());
  }

  // A run that stops short of the finish (a spin, a red flag) ends untimed
  {
    LapTimer timer;
    timer.begin(at(0, 0), {}, MIN_LAP_TIME_MS);
    timer.setStage(at(300, 200, 0), true);
    Car c(timer);
    c.wait(3);
    c.driveTo(200, 0, 70);
    c.stopWithin(30, 0, 70);
    c.wait(6);
    CHECK(!timer.lapRunning() && timer.lapCount() == 0 && c.count(LapEventType::RunEnded) == 1, "stopped short: no time");
    // Crossing the finish the wrong way (driving back down through it) is nothing either
    c.x = 300;
    c.y = 260;
    c.driveTo(300, 150, 40);
    CHECK(timer.lapCount() == 0, "wrong way through the finish");
  }

  // A circuit is untouched by any of it
  {
    LapTimer timer;
    timer.begin(at(0, 0), {}, MIN_LAP_TIME_MS);
    CHECK(!timer.isStage() && !timer.armed(), "a circuit isn't a stage");
  }

  if (failures) {
    printf("%d FAILED\n", failures);
    return 1;
  }
  printf("All point to point tests passed\n");
  return 0;
}
