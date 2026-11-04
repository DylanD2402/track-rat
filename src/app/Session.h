/**
 * The session, moved out of main.cpp: lap timing (the Running screen and its Summary), Free Roam, tracks that learn
 * themselves, and the SYNC flash and camera start notes that go into the session's summary. They share the session's
 * files (the .vbo log and the .json summary) and the replay of kept fixes.
 */
#ifndef SESSION_H
#define SESSION_H

#include <Arduino.h>
#include "App.h"
#include "InputManager.h"

namespace Session {

void stateChanging(AppState next);      // enterState(): a lap-timing session starts or ends (state is still the old screen)
void freeRoamChanging(AppState next);   // enterState(): Free Roam starts or ends

void onInputRunning(InputEvent ev);     // (not input...: that would hide the global `input`)
void onInputSummary(InputEvent ev);
void onInputFreeRoam(InputEvent ev);
void onInputFreeRoamSummary(InputEvent ev);

void beforeRender(uint32_t now);        // render(): what the timing screens read, set before any of them is drawn
void renderRunning(uint32_t now);
void renderSummary();
void renderFreeRoam();
void renderFreeRoamSummary();

void onFixRunning(const GpsFix& fix);   // loop(): a fix while timing
void onFixFreeRoam(const GpsFix& fix);  // loop(): a fix in Free Roam
bool parked();                          // parked in the pits
void replayTick();                      // loop(): the kept fixes of a learned track, a few at a time
void noteCameraStart();                 // loop(): the camera started recording
void poll(uint32_t now);                // loop(): once a pass
void flushIfDue(uint32_t now);          // loop(): saves the summary a moment after a lap

// For the other screens
void flushSummary();                    // saves the summary now if it is waiting (before reading it, sleeping, Wi-Fi)
void keepLayout();                      // the driver picked this track: the layout check doesn't swap it
void markNewLine();                     // the Ready screen's Set line: the session starts by marking the line
void notice(const String& text, uint32_t showMs);  // a note on the timing screen for showMs

}  // namespace Session

#endif
