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

void begin();
void sendFix(const GpsFix& fix, const GPSHandler& gps);
bool connected();

}  // namespace RaceBoxBle

#endif  // RACEBOX_BLE_H
