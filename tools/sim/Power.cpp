// Stand-in for src/Power.cpp (tools/build_sim.py): a battery at 76 %, not charging. The backlight is a number
// JavaScript reads with the picture. Switching off or sleeping ends the run: JavaScript throws, and the
// wrapper reports the unit as off. Same header (include/Power.h).
#include "Power.h"
#include "Config.h"
#include "sim.h"

namespace Power {

static bool backlightOn = false;
static uint8_t brightnessPct = 100;

void begin() {}
void update(uint32_t) {}
char takeTestKey() { return 0; }

int batteryPercent() { return 76; }
int batteryMillivolts() { return 3980; }
bool batteryCharging() { return false; }
bool batteryLow(uint32_t) { return false; }

battery::Report batteryReport() {
  static battery::Stats stats({BATTERY_CAPACITY_MAH,
                               {BATTERY_DRAW_SCREEN_MA, BATTERY_DRAW_WIFI_MA, BATTERY_DRAW_SCREEN_OFF_MA, BATTERY_DRAW_SLEEP_MA}});
  battery::Report r = stats.report(battery::Mode::ScreenOn, 76, false);
  r.present = true;
  r.percent = 76;
  r.mv = 3980;
  r.charging = false;
  return r;
}

void backlight(bool on) { backlightOn = on; }
void setBrightness(uint8_t percent) { brightnessPct = percent; }

void deepSleep(bool) { sim::halt(sim::Halt::Off); }

}  // namespace Power

namespace sim {
int backlightPercent() { return Power::backlightOn ? Power::brightnessPct : 0; }
}  // namespace sim
