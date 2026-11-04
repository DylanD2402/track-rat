/**
 * Performance mode, acceleration and braking runs: the screen's state, input and drawing, moved out of main.cpp.
 * main.cpp calls these at the places where the screen starts, ends, takes a tap, draws and gets a fix.
 */
#ifndef PERFORMANCE_MODE_H
#define PERFORMANCE_MODE_H

#include "GPSHandler.h"
#include "InputManager.h"

namespace PerformanceMode {

void enter();                   // the screen starts
void leave();                   // the screen ends
void onInput(InputEvent ev);    // a tap, hold or BOOT press (not input(): that would hide the global `input`)
void render();                  // draw the screen
void onFix(const GpsFix& fix);  // a new GPS fix
bool still();                   // nothing live on the screen: it can be drawn less often

}  // namespace PerformanceMode

#endif
