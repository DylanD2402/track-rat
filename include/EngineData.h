/**
 * Track Rat - the engine's figures, from whichever way in is fitted
 *
 * OBD-II over CAN (CanObd.h, 5.2) or a Bluetooth LE adapter (ObdBle.h, 6.1):
 * the log's engine channels, the shift light and the coolant warning read
 * them here, so they don't care which. CAN wins when both are there.
 */

#ifndef ENGINE_DATA_H
#define ENGINE_DATA_H

#include "Obd.h"

namespace EngineData {

bool logged();                // the CAN controller runs, or an adapter has been chosen: logs get the engine channels
const obd::Values& values();  // the latest figures (check ...Known() for each)
bool coolantHot(uint32_t nowMs);  // known, and at COOLANT_WARN_C or more

}  // namespace EngineData

#endif  // ENGINE_DATA_H
