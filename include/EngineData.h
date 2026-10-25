/**
 * Track Rat - the engine's figures, from whichever way in is fitted
 *
 * OBD-II over CAN (CanObd.h, 5.2) or a Bluetooth LE adapter (ObdBle.h, 6.1):
 * the log's engine channels, the shift light and the coolant tag read
 * them here, so they don't care which. CAN wins when both are there.
 */

#ifndef ENGINE_DATA_H
#define ENGINE_DATA_H

#include "CarObd.h"
#include "Obd.h"

namespace EngineData {

bool logged();                // the CAN controller runs, or an adapter has been chosen: logs get the engine channels
const obd::Values& values();  // the latest figures (check ...Known() for each)
// The temperatures, if the engine has sent them in the last 3 s (obd::SLOW_STALE_MS)
bool coolant(uint32_t nowMs, float& c);
bool oil(uint32_t nowMs, float& c);

// Car-specific data (a car profile, through the Bluetooth adapter): the
// profile in use (nullptr: none), and its signal i if it has been read
// lately (in the last 3 of its requests' periods, at least 1 s).
const carobd::Profile* carProfile();
bool carValue(int i, uint32_t nowMs, float& v);

}  // namespace EngineData

#endif  // ENGINE_DATA_H
