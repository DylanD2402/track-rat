/**
 * The Track day list and a track's Ready screen, moved out of main.cpp: the list of tracks nearest first, the Ready
 * screen that starts a session, the PBs they show, and the world tracks read from the card for where we are.
 */
#ifndef TRACK_DAY_H
#define TRACK_DAY_H

#include <Arduino.h>
#include "App.h"
#include "InputManager.h"

namespace TrackDay {

void stateChanging(AppState next);  // enterState(): a screen is about to change to `next` (state is still the old one)
void onInputList(InputEvent ev);    // a tap or press on the Track day list (not input...: that would hide the global `input`)
void renderList();
void onInputReady(InputEvent ev);   // a track's Ready screen
void renderReady();
bool readyCanStart();  // the Ready screen's track may start a session from here (TrackRange.h)

// A track's best lap, read from the card once and kept (ms 0: none)
void personalBestFor(int index, uint32_t& ms, uint32_t& date);
void forgetPb(int index);  // the track's PB changed: read it again
void forgetPbs();          // any of them may have changed (a re-time, another driver)

void refreshWorldTracks();  // the world tracks near here, on the screens before a session
void addBenchTrack();       // setup(): the Bench Test, a simulated car
void createNewTrack();      // + New circuit

}  // namespace TrackDay

#endif
