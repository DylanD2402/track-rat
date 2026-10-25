/**
 * Track Rat - drivers and cars (kept in flash)
 *
 * Profiles::get() is the lists for any code to read; the rules (names, ids,
 * limits) are in Profiles.h. The lists are loaded once at boot and written to
 * flash (NVS, as one small JSON text) only when something changes. The Wi-Fi
 * page's web server calls the edit functions from its own task, the Bluetooth
 * OBD task reports the car's VIN, and the screens read: all safe from any core.
 *
 * Best laps and session files go with the driver and car of the session, not
 * with whatever is chosen now: lockSession() fixes the pair when a session
 * starts and unlockSession() lets go of it when it is over, so a change made
 * meanwhile (the car's VIN arriving late) waits for the next session.
 *
 * These are the driver's cars. The OBDb "car profiles" on the SD card
 * (CarProfiles.h) are a different thing, matched to a car by its VIN.
 */

#ifndef PROFILE_STORE_H
#define PROFILE_STORE_H

#include <Arduino.h>
#include "Profiles.h"

namespace Profiles {

void begin();  // load from flash; call before anything that reads get()
profiles::Book get();
uint32_t changeCount();  // goes up by one with every change (the screens redraw)

// Edits: each validates, uses and saves, and says why not (profiles::errText)
profiles::Err add(profiles::Kind k, const char* name, int* newId = nullptr);
profiles::Err rename(profiles::Kind k, int id, const char* name);
profiles::Err remove(profiles::Kind k, int id);
profiles::Err select(profiles::Kind k, int id);
profiles::Err setVin(int carId, const char* vin);

// The current driver and car, as the screens show them
bool showLabel();
void label(char* out, size_t n);

// The pair a session is recorded under: fixed from its start to its end, else the current ones
void lockSession();    // a session starts (does nothing if one is already locked in)
void unlockSession();  // no session is running
bool sessionLocked();
struct Active {
  int driverId = 1, carId = 1;
  char driver[profiles::NAME_MAX + 1] = "";
  char car[profiles::NAME_MAX + 1] = "";
};
Active active();
// Where best laps are kept inside a track's folder: "" (the track's folder itself) or e.g. "d3-c2"
String pbFolder();

// The car's VIN from the Bluetooth OBD adapter. noteVin() remembers it (the OBD task);
// applyVin() makes the car that has this VIN the current one, unless a session is running
// (the main loop, which calls it often: it does nothing unless there is a new VIN). True if the car changed.
void noteVin(const char* vin);
void vinSeen(char* out, size_t n);  // the last VIN seen since boot, "" if none
bool applyVin();

// The Wi-Fi page's view: {"drivers":[{"id","name"}],"cars":[{"id","name","vin"}],"driver":id,"car":id,"vinSeen":"..."}
String apiJson();

}  // namespace Profiles

#endif  // PROFILE_STORE_H
