/**
 * Track Rat - the RaceChrono Bluetooth profile (6.3)
 *
 * With Settings > Bluetooth profile on RaceChrono, Track Rat is a RaceChrono
 * "DIY" Bluetooth LE device instead of a RaceBox Mini: service 0x1FF8 with its
 * GPS at the fix rate and the engine's values (and the IMU's g, when fitted)
 * as CAN-style packets that RaceChrono channels read (RaceChrono.h). One
 * profile at a time: a RaceChrono forum report found iOS stopped receiving DIY
 * data with a second UART-style service present, and RaceBox uses one.
 *
 * RaceBoxBle.cpp owns the Bluetooth server and calls these. Untested with
 * RaceChrono (the user will try it on their iPhone).
 */

#ifndef RACECHRONO_BLE_H
#define RACECHRONO_BLE_H

#include <NimBLEDevice.h>
#include "GPSHandler.h"

namespace RaceChronoBle {

void build(NimBLEServer* server);  // the service and its four characteristics; then advertise 0x1FF8
void sendFix(const GpsFix& fix, bool hasFix, bool connected);  // GPS, then any engine packets due
void onDisconnect();               // the app's filter goes with it

}  // namespace RaceChronoBle

#endif  // RACECHRONO_BLE_H
