/**
 * Track Rat - OBD-II over CAN (optional, CAN_ENABLED in Config.h, off by default)
 *
 * The ESP32-S3's own CAN controller (TWAI) with an SN65HVD230 transceiver on
 * IO3 (TX) and IO14 (RX), wired to the car's OBD-II socket (CAN high pin 6,
 * CAN low pin 14, ground pin 4 or 5). It asks the engine for its speed,
 * throttle and coolant temperature a few times a second (Obd.h): they go
 * into the .vbo log, and the coolant temperature feeds the overheat warning.
 *
 * Off by default because IO14 is also one of the GPS pins on the expansion
 * header; it stays off if the GPS was found there. Untested on hardware: no
 * transceiver has been fitted yet.
 */

#ifndef CAN_OBD_H
#define CAN_OBD_H

#include "Obd.h"

namespace CanObd {

void begin(int gpsRxPin, int gpsTxPin);  // after the GPS has been found
bool active();                           // the CAN controller is running
void poll();                             // from loop(): never waits
const obd::Values& values();             // the latest figures (check ...Known() for each)

}  // namespace CanObd

#endif  // CAN_OBD_H
