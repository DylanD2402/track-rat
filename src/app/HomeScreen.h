/**
 * The home screen, the GPS wait and GPS status screens, the Next up card, and what happens by itself before a
 * session (picking the track at boot, driving off from the menu), moved out of main.cpp.
 */
#ifndef HOME_SCREEN_H
#define HOME_SCREEN_H

#include <Arduino.h>
#include "App.h"
#include "GPSHandler.h"
#include "InputManager.h"

namespace HomeScreen {

void stateChanging(AppState next);  // enterState(): a screen is about to change to `next` (state is still the old one)
void onInputGpsWait(InputEvent ev);    // a tap or press (not input...: that would hide the global `input`)
void renderGpsWait();
void onInputMenu(InputEvent ev);
void renderMenu(uint32_t now);         // (now: the loop's millis(), as render() read it)
void onInputGpsStatus(InputEvent ev);
void renderGpsStatus();

// Once the GPS has a fix: picks a track within range, or leaves the driver on the menu
void autoPickTrack();
// On the menu with no track within range: driving off starts Free Roam
bool awayFromTracks();
void onFix(const GpsFix& fix);
// The world tracks changed: the Next up card is worked out again
void forgetNextUp();

}  // namespace HomeScreen

#endif
