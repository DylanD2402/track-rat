/**
 * The Settings screens and the first-boot questions: Settings, Engine data, Camera, Battery and Setup, moved out of
 * main.cpp. The rules are in SettingsFlow.h and DeviceScreen.h; this is the state and the glue to the rest of the app.
 */
#ifndef SETTINGS_SCREENS_H
#define SETTINGS_SCREENS_H

#include <Arduino.h>
#include "InputManager.h"

namespace SettingsScreens {

void open();                      // the home screen's Settings button: starts the list and switches to it
void onInputList(InputEvent ev);  // a tap or press on Settings (not input...: that would hide the global `input`)
void renderList();
void onInputEngine(InputEvent ev);  // Settings > Engine data
void renderEngine();
void onInputCamera(InputEvent ev);  // Settings > Camera
void renderCamera();
void onInputBattery();  // Settings > Battery
void renderBattery();
void onInputSetup(InputEvent ev);  // the first-boot questions
void renderSetup();
bool followDrag(uint32_t now);  // each loop: a finger dragging the Settings list moves it; true while it does

}  // namespace SettingsScreens

#endif
