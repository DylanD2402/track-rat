/**
 * Track Rat - the DJI camera's GPS remote over Bluetooth (6.2)
 *
 * Track Rat does the job of DJI's GPS remote for an Osmo Action 4, 5 Pro, 6
 * or Osmo 360 (DjiRemote.h): it sends its GPS 10 times a second, which the
 * camera writes into the video (DJI Mimo and DJI Studio then draw speed and
 * route dashboards), and starts and stops recording with the sessions.
 *
 * Settings > Camera > Pair scans for DJI cameras; tap yours and accept on the
 * camera if its code matches the one on Track Rat's screen. After that Track
 * Rat reconnects by itself (every 5-10 s while the camera is off or away).
 * Nothing happens until a camera has been paired. The camera has one remote
 * slot, so DJI Mimo can't be its remote at the same time.
 *
 * The Bluetooth work runs in its own task on core 0, never in loop(). The
 * protocol is subject to DJI's licence terms. Untested with a camera.
 */

#ifndef CAMERA_BLE_H
#define CAMERA_BLE_H

#include <stdint.h>
#include "DjiRemote.h"
#include "GpsFix.h"

namespace CameraBle {

void begin();  // at boot: starts the task if a camera has been paired
void poll();   // from loop(): takes the latest status from the task. Never waits

// Each GPS fix (sent on at 10 Hz while connected)
void onFix(const GpsFix& fix, bool hasFix);
// Whether a session wants the camera recording (with Camera recording on "With sessions")
void setRecording(bool want);

bool configured();  // a camera has been paired
bool connected();   // and the remote handshake is done
// The camera started recording since the last call: millis() of the status that said so
bool takeStarted(uint32_t& atMs);

struct Status {
  bool configured = false, linked = false;  // linked: Bluetooth connected
  dji::Remote::Phase phase = dji::Remote::Phase::Off;
  bool pairing = false;   // asking the camera to accept us
  uint16_t code = 0;      // the code it should show
  char name[24] = "";
  char address[18] = "";
  dji::Status cam;        // its last status (cam.known once one came)
  uint32_t gpsSent = 0;   // GPS pushes since connecting
};
Status status();

// Pair: the scan's list, strongest first
struct Found {
  char name[24];
  char address[18];
  uint8_t addressType;
  int rssi;
};
constexpr int MAX_FOUND = 6;
void startScan();
bool scanning();
int found(Found* out, int max);
void choose(const Found& f);  // pair with it (the camera asks the driver to accept)
void forget();

}  // namespace CameraBle

#endif  // CAMERA_BLE_H
