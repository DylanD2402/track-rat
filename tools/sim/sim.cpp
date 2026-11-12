// The simulated unit's hardware and its door to JavaScript (tools/build_sim.py, site/try/host.js).
//
// The firmware's own code runs unchanged: main.cpp's setup() and loop(), the screens, the lap timer. What is
// stood in for here is what a browser does not have: the clock (virtual: JavaScript sets it on every tick),
// the touch controller and the BOOT button (answered from the finger JavaScript puts on the screen), the serial
// log, the background tasks (queued, run to the end after the loop's pass), and the things that never return
// on the device (sleep, restart: JavaScript throws, so the tick unwinds).
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <Arduino.h>
#include <sys/time.h>
#include <Wire.h>
#define private public  // (the picture and the touch state are private members)
#include "app/App.h"
#undef private
#include "InputManager.h"
#include "LapTimer.h"
#include "UiTheme.h"
#include "sim.h"
#include "vfs.h"

void setup();
void loop();

#define EXPORT(name) extern "C" __attribute__((export_name(name)))
#define IMPORT(name) extern "C" __attribute__((import_module("env"), import_name(name)))

IMPORT("trackrat_halt") void trackrat_halt(int why);
IMPORT("trackrat_log") void trackrat_log(const char* line, int len);
IMPORT("trackrat_set_realtime") void trackrat_set_realtime(double ms);

uint32_t mockMillis = 0;

// ---------------------------------------------------------------------------
// Serial: the log, a line at a time

static std::string logLine;

size_t HardwareSerial::write(uint8_t c) {
  if (this != &Serial) return 1;  // (Serial1 is the GPS's wire: nothing there)
  if (c == '\n') {
    trackrat_log(logLine.data(), (int)logLine.size());
    logLine.clear();
  } else if (c != '\r') {
    logLine += (char)c;
  }
  return 1;
}
size_t HardwareSerial::write(const uint8_t* b, size_t n) {
  for (size_t i = 0; i < n; i++) write(b[i]);
  return n;
}
HardwareSerial Serial;
HardwareSerial Serial1;
EspClass ESP;

namespace sim {
void halt(Halt why) {
  trackrat_halt((int)why);
  abort();  // (JavaScript has thrown; this is not reached)
}
}  // namespace sim

void EspClass::restart() { sim::halt(sim::Halt::Restart); }

// ---------------------------------------------------------------------------
// The finger, the BOOT button and the touch controller

static bool fingerDown = false, bootDown = false;
static int fingerX = 0, fingerY = 0;  // where it is on the picture (0..319, 0..239)

int digitalRead(int pin) { return pin == BOOT_BUTTON_PIN && bootDown ? LOW : HIGH; }

TwoWire Wire;

// The FT6336 register read InputManager makes: status byte, then X high/low and Y high/low of the first point,
// in the panel's own portrait coordinates. InputManager turns them into screen coordinates (swap, invert, and the
// turn-round of an upside-down unit); this is that turned back, so a touch lands where it is seen.
uint8_t TwoWire::requestFrom(uint8_t addr, uint8_t n, uint8_t) {
  have_ = pos_ = 0;
  if (addr != 0x38 || n != 5 || reg_ != 0x02) return 0;
  int sx = fingerX, sy = fingerY;
  if (input.flipped) sx = ui::W - 1 - sx, sy = ui::H - 1 - sy;
  const int rawY = std::max(0, std::min(319, sx));
  const int rawX = std::max(0, std::min(239, ui::H - 1 - sy));
  buf_[0] = fingerDown ? 1 : 0;
  buf_[1] = (rawX >> 8) & 0x0F;
  buf_[2] = rawX & 0xFF;
  buf_[3] = (rawY >> 8) & 0x0F;
  buf_[4] = rawY & 0xFF;
  have_ = 5;
  return 5;
}

// ---------------------------------------------------------------------------
// Tasks: queued, and run to the end after the loop's pass

namespace {
struct Task {
  TaskFunction_t fn;
  void* arg;
};
std::deque<Task> tasks;

void runTasks() {
  while (!tasks.empty()) {
    const Task t = tasks.front();
    tasks.pop_front();
    t.fn(t.arg);
  }
}
}  // namespace

extern "C" BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, uint32_t, void* arg, UBaseType_t, TaskHandle_t* handle,
                                              BaseType_t) {
  tasks.push_back({fn, arg});
  if (handle) *handle = (TaskHandle_t)1;
  return pdPASS;
}
extern "C" void vTaskDelete(TaskHandle_t) {}

// ---------------------------------------------------------------------------
// The system clock: the wall clock JavaScript keeps (WASI's clock_time_get), set by the GPS time

extern "C" void tzset(void) {}

extern "C" int settimeofday(const struct timeval* tv, const void*) {
  if (tv) trackrat_set_realtime((double)tv->tv_sec * 1000.0 + tv->tv_usec / 1000.0);
  return 0;
}

// ---------------------------------------------------------------------------
// The door to JavaScript

static const char* STATE_NAMES[] = {"GPS_WAIT", "MENU",      "TRACK_SELECT", "TRACK_CONFIRM",     "RUNNING",
                                    "SUMMARY",  "GPS_STATUS", "FREE_ROAM",   "FREE_ROAM_SUMMARY", "PERFORMANCE",
                                    "WIFI",     "SETUP",      "SETTINGS",     "ENGINE_DATA",       "CAMERA",
                                    "AUTOCROSS", "NEW_COURSE", "ADJUST_LINE", "BATTERY"};

static std::vector<uint8_t> exported;
static char debugText[96];

EXPORT("sim_alloc") void* sim_alloc(int n) { return malloc((size_t)n); }
EXPORT("sim_free") void sim_free(void* p) { free(p); }

// The clock at power-on. Before setup().
EXPORT("sim_init") void sim_init(double startMs) { mockMillis = (uint32_t)(uint64_t)startMs; }

// The built-in track list, /tracks.json on the flash volume. Before setup().
EXPORT("sim_set_tracks") void sim_set_tracks(const uint8_t* p, int n) { sim::putFlashFile("/tracks.json", p, (size_t)n); }

// What sim_export gave earlier. Before setup(). 1: taken.
EXPORT("sim_import") int sim_import(const uint8_t* p, int n) { return sim::importSaved(p, (size_t)n) ? 1 : 0; }

EXPORT("sim_setup") void sim_setup() {
  setup();
  runTasks();
}

EXPORT("sim_tick") void sim_tick(double nowMs) {
  const uint32_t t = (uint32_t)(uint64_t)nowMs;
  if ((int32_t)(t - mockMillis) > 0) mockMillis = t;  // never backwards
  loop();
  runTasks();
}

EXPORT("sim_touch") void sim_touch(int x, int y, int down) {
  fingerX = std::max(0, std::min(ui::W - 1, x));
  fingerY = std::max(0, std::min(ui::H - 1, y));
  fingerDown = down != 0;
}

EXPORT("sim_button") void sim_button(int down) { bootDown = down != 0; }

EXPORT("sim_gps_fix")
void sim_gps_fix(double lat, double lon, double speedKmh, double headingDeg, double accuracyM, double utcMs, int sats) {
  sim::gpsFix(lat, lon, speedKmh, headingDeg, accuracyM, utcMs, sats);
}
EXPORT("sim_gps_off") void sim_gps_off() { sim::gpsOff(); }

// The picture: 320 x 240 RGB565 pixels, upright whichever way up the unit is set
EXPORT("sim_frame") const uint16_t* sim_frame() { return display.tft.pixels(); }
EXPORT("sim_backlight") int sim_backlight() { return sim::backlightPercent(); }

EXPORT("sim_state_seq") double sim_state_seq() { return sim::savedSeq(); }

EXPORT("sim_export") const uint8_t* sim_export() {
  exported = sim::exportSaved();
  return exported.data();
}
EXPORT("sim_export_len") int sim_export_len() { return (int)exported.size(); }

// "RUNNING,2,153123": the screen, the laps timed, the last lap in ms
EXPORT("sim_debug") const char* sim_debug() {
  const Lap* last = lapTimer.lastLap();
  snprintf(debugText, sizeof(debugText), "%s,%u,%lu", STATE_NAMES[(int)state], (unsigned)lapTimer.lapCount(),
           (unsigned long)(last ? last->timeMs : 0));
  return debugText;
}
