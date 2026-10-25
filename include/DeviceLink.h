/**
 * Track Rat - "is the Bluetooth device really there?" (the OBD-II adapter and the DJI camera)
 *
 * A remembered adapter or camera that is out of reach, unplugged or switched off must show
 * nothing in the top bar and nothing as "Connected" in Settings. The Bluetooth link alone
 * can't say: it only reports being down after a supervision timeout (seconds), and is "up"
 * from the moment of connecting, before the device has said a word. So a device counts as
 * connected only while the link is up AND it has sent something to us recently.
 * "Recently" is OBD_ALIVE_MS for the adapter (any bytes) and CAMERA_ALIVE_MS for the camera
 * (its status, about twice a second). Pure C++, tested on a PC in test/test_devicelink.cpp.
 */

#ifndef DEVICE_LINK_H
#define DEVICE_LINK_H

#include <stdint.h>
#include "Config.h"

namespace devicelink {

// linked: the Bluetooth link is up (and, for the camera, the handshake is done).
// lastMsgAt: millis() when the device last sent anything on this link (0: nothing yet).
// A message stamped a moment after `now` (the other core got there first) counts as just now.
inline bool alive(bool linked, uint32_t lastMsgAt, uint32_t now, uint32_t windowMs) {
  return linked && lastMsgAt != 0 && (int32_t)(now - lastMsgAt) <= (int32_t)windowMs;
}

inline bool obdAlive(bool linked, uint32_t lastMsgAt, uint32_t now) { return alive(linked, lastMsgAt, now, OBD_ALIVE_MS); }
inline bool cameraAlive(bool linked, uint32_t lastStatusAt, uint32_t now) { return alive(linked, lastStatusAt, now, CAMERA_ALIVE_MS); }

}  // namespace devicelink

#endif  // DEVICE_LINK_H
