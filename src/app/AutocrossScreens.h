/**
 * The Autocross screens: the hub, New course and Adjust start / Adjust finish, moved out of main.cpp.
 * main.cpp calls these where the screens change, take a tap, draw and are polled.
 */
#ifndef AUTOCROSS_SCREENS_H
#define AUTOCROSS_SCREENS_H

#include <Arduino.h>
#include "App.h"
#include "InputManager.h"

namespace AutocrossScreens {

void stateChanging(AppState next);    // enterState(): a screen is about to change to `next` (state is still the old one)
void onInputHub(InputEvent ev);       // a tap or press on the hub (not input...: that would hide the global `input`)
void renderHub();
void onInputAdjust(InputEvent ev);    // Adjust start / Adjust finish
void renderAdjust();
void onInputNewCourse(InputEvent ev);
void renderNewCourse();
void poll(uint32_t now);              // each loop: the day's logs for Adjust, the re-time ending, a held arrow repeating

}  // namespace AutocrossScreens

#endif
