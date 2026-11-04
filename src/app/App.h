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
extern uint32_t syncUntil;      // millis() the SYNC flash ends; 0: not showing
extern GpsFix cameraFix;        // the latest fix, to turn millis() into GPS time
extern bool haveCameraFix;      // cameraFix has a date

// Switches screen: ends the old one and starts the new one
void enterState(AppState next);
// The picked track
const Track& selectedTrack();
// Makes the current settings take effect (screen and touch the right way up, backlight, Bluetooth, time zone)
void applySettings();
// Autocross courses (Courses.h), by trackDB index
std::vector<courses::Candidate> courseCandidates();
// Today's local date, "YYYYMMDD" ("" if the GPS hasn't given one)
std::string todayEvent();
// Is the Ready screen waiting for the car to drive off (AutoStart.h)
bool autoStartArmed();
// A track was picked at boot with the car parked: the Ready screen starts the session when the car drives off
void armAutoStart();
// Metres from here to a track's reference point (-1: no GPS fix, or the Bench Test)
double distanceToTrack(int index);
// trackDB index of today's course nearby (made today, start within 5 km), or -1
int todaysCourseNearby();

// A session ended by itself: the summary starts the next one on driving off
void armNextSession();
// "2026-10-20T14:03:12.350Z": the GPS time at millis() `at`, from the latest fix
String gpsTimeAt(const GpsFix& f, uint32_t at);

#endif
