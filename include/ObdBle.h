/**
 * Track Rat - engine data from a Bluetooth LE OBD-II adapter (6.1)
 *
 * Track Rat as a Bluetooth central, beside its RaceBox peripheral role: it
 * connects to an ELM327 adapter plugged into the car (Veepeak OBDCheck BLE+,
 * OBDLink CX, vLink...) and asks for RPM, throttle, coolant and intake
 * temperature (Elm327.h). The values go where the CAN path's go (EngineData.h):
 * the log and the coolant warning.
 *
 * Settings > Engine data > Find adapter scans for OBD_SCAN_S seconds and
 * lists what it finds; the one tapped is remembered (in flash) and Track Rat
 * reconnects to it by itself, every 5-10 s while it's out of reach. Nothing
 * happens until an adapter has been chosen.
 *
 * The Bluetooth work (connecting can take seconds) runs in its own task on
 * core 0, never in loop(). Untested on hardware: no adapter has been tried yet.
 */

#ifndef OBD_BLE_H
#define OBD_BLE_H

#include <stdint.h>
#include "Elm327.h"

namespace ObdBle {

void begin();  // at boot, after the settings: starts the task if an adapter has been chosen
void poll();   // from loop(): takes the latest values and status from the task. Never waits

bool configured();             // an adapter has been chosen
bool connected();              // and it is there: the link is up AND it has sent something within OBD_ALIVE_MS (DeviceLink.h).
                               // A remembered adapter that is away, unplugged or still connecting is not connected
bool readingEngine();          // and the car's engine is answering through it: the top bar's OBD tag. Not while the
                               // adapter is still finding the car (SEARCHING...) or the ignition is off (NO DATA)
const obd::Values& values();   // the latest figures (check ...Known() for each)

// The Engine data screen
struct Status {
  bool configured = false;
  bool linked = false;      // the Bluetooth link is up (the adapter may not have answered yet)
  bool connected = false;   // linked, and the adapter has sent something recently (set by poll())
  uint32_t lastRxAt = 0;    // millis() when the adapter last sent anything on this link (0: nothing yet)
  elm::Client::Phase phase = elm::Client::Phase::Off;
  char name[24] = "";       // the adapter's name, as it advertised it
  char address[18] = "";
  char protocol[40] = "";   // "AUTO, ISO 15765-4 (CAN 11/500)"
  elm::Client::Stats stats;
  bool suffix = false, multi = false;
  // Car-specific data (CarObd.h, a profile on the SD card)
  int profiles = 0;                          // car profiles on the card
  char vin[18] = "";                         // the car's VIN, "" if it didn't say (or not asked yet)
  bool vinDone = false;
  const carobd::Profile* car = nullptr;      // the profile in use, or nullptr
  carobd::Answer answers[carobd::MAX_COMMANDS] = {};
  float carHz[carobd::MAX_COMMANDS] = {};
};
Status status();
// The car's profile in use (nullptr: none) and its latest values (check Values::known)
const carobd::Profile* carProfile();
const carobd::Values& carValues();

// Find adapter
struct Found {
  char name[24];
  char address[18];
  uint8_t addressType;
  int rssi;
};
constexpr int MAX_FOUND = 6;
void startScan();             // OBD_SCAN_S seconds, in the background
bool scanning();
int found(Found* out, int max);  // the adapters found by the last scan, strongest first
void choose(const Found& f);  // remember it and connect
void forget();                // disconnect and forget it

}  // namespace ObdBle

#endif  // OBD_BLE_H
