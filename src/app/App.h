/**
 * What every screen needs: the state shared by main.cpp and the screen files in src/app.
 * The objects are defined in main.cpp; a screen file includes this and uses them as they are.
 */
#ifndef APP_H
#define APP_H

#include <Arduino.h>
#include <string>
#include <vector>
#include "Courses.h"
#include "DisplayManager.h"
#include "FreeRoam.h"
#include "GPSHandler.h"
#include "InputManager.h"
#include "LapTimer.h"
#include "TrackDatabase.h"

enum class AppState { GpsWait, Menu, TrackSelect, TrackConfirm, Running, Summary, GpsStatus, FreeRoam, FreeRoamSummary, Performance, Wifi,
                      Setup, SettingsScreen, EngineScreen, CameraScreen, Autocross, NewCourse, AdjustLine, BatteryScreen };

extern DisplayManager display;  // the screen
extern GPSHandler gps;          // the GPS receiver and its latest fix
extern InputManager input;      // touch and the BOOT button
extern LapTimer lapTimer;       // the lap-timing session
extern TrackDatabase trackDB;   // the tracks on the card
extern FreeRoam freeRoam;       // the Free Roam session
extern AppState state;          // the screen being shown
extern uint32_t lastRender;     // millis() of the last redraw; 0 makes the next loop redraw at once
extern int selectedIndex;       // the picked track (a trackDB index)

// Switches screen: ends the old one and starts the new one
void enterState(AppState next);
// The picked track
const Track& selectedTrack();
// Saves the session's summary now if it is waiting
void flushSummary();
// Makes the current settings take effect (screen and touch the right way up, backlight, Bluetooth, time zone)
void applySettings();
// Autocross courses (Courses.h), by trackDB index
std::vector<courses::Candidate> courseCandidates();
// Today's local date, "YYYYMMDD" ("" if the GPS hasn't given one)
std::string todayEvent();
// The session's track was picked by the driver, so the layout check doesn't swap it for one nearby
void sessionKeepLayout();
// A note on the timing screen for showMs
void sessionNotice(const String& text, uint32_t showMs);

#endif
