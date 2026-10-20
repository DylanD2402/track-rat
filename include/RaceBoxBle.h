/**
 * Track Rat - Bluetooth LE GPS output (RaceBox Mini compatible)
 *
 * Advertises as "RaceBox Mini <10 digits>" with the Nordic UART service, and
 * sends one RaceBox data packet per GPS fix to a connected phone. Apps that
 * support the RaceBox Mini (RaceChrono, Solostorm, ...) can use Track Rat as
 * their GPS. The board's own lap timer keeps running alongside.
 *
 * Not affiliated with RaceBox; the name is only used for app compatibility.
 */

#ifndef RACEBOX_BLE_H
#define RACEBOX_BLE_H

#include <Arduino.h>
#include "GPSHandler.h"

namespace RaceBoxBle {

void initRadio();  // start the Bluetooth stack (once); begin() and the OBD adapter both need it
void begin();
void setEnabled(bool on);  // the Bluetooth setting: the first "on" starts Bluetooth
void sendFix(const GpsFix& fix, const GPSHandler& gps);
bool connected();
void resumeAdvertising();  // after another connection (the OBD adapter): keep advertising to phones

// One connection attempt or scan at a time across the Bluetooth centrals (the OBD
// adapter, the camera): NimBLE refuses a second. Any task may unlock (a scan's
// end comes from NimBLE's own task).
bool radioTryLock(uint32_t waitMs);
void radioUnlock();

}  // namespace RaceBoxBle

#endif  // RACEBOX_BLE_H
