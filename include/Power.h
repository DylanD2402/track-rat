/**
 * Track Rat - Power
 *
 * Battery voltage (IO9 through the board's divider), the backlight, and deep
 * sleep. The maths lives in Battery.h and SleepTimer.h.
 */

#ifndef POWER_H
#define POWER_H

#include <Arduino.h>

namespace Power {

// Call first thing in setup(): frees the backlight pin after a deep sleep and sets up the ADC.
void begin();

// Call every loop. Reads the battery about once a second.
void update(uint32_t nowMs);

int batteryPercent();               // 0..100, or -1 when no battery is fitted
int batteryMillivolts();            // 0 when no battery is fitted
bool batteryLow(uint32_t nowMs);    // low for long enough to save and sleep

// Backlight on (at the brightness set below) or off.
void backlight(bool on);
// Brightness for the backlight when it's on, 10..100 percent (the Brightness setting).
void setBrightness(uint8_t percent);

// Backlight off, then deep sleep until BOOT is pressed or the screen is touched. Does not return.
// The caller has already saved any session.
[[noreturn]] void deepSleep();

}  // namespace Power

#endif  // POWER_H
