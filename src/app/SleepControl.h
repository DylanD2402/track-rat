/**
 * Sleep, power off and battery: the auto-sleep timer, the screen going off, deep sleep and Power off, moved out
 * of main.cpp. main.cpp calls these where the board is set up, polled and touched.
 */
#ifndef SLEEP_CONTROL_H
#define SLEEP_CONTROL_H

#include <Arduino.h>

namespace SleepControl {

void begin(uint32_t now);         // setup() is done: the idle clock starts
bool screenOff();                 // the screen is off (parked and idle)
bool noteActivity(uint32_t now);  // something happened (a touch, a press, the car moving); true if the screen was off
void poll(uint32_t now);          // each loop: the battery, the idle clock, the screen going off and deep sleep
[[noreturn]] void powerOff();     // everything saved, then off until the screen is tapped

}  // namespace SleepControl

#endif
