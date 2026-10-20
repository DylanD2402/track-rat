/**
 * Track Rat - the engine's figures (see EngineData.h)
 */

#include "EngineData.h"
#include "CanObd.h"
#include "Config.h"
#include "ObdBle.h"

namespace EngineData {

bool logged() { return CanObd::active() || ObdBle::configured(); }

const obd::Values& values() { return CanObd::active() ? CanObd::values() : ObdBle::values(); }

bool coolantHot(uint32_t nowMs) {
  const obd::Values& v = values();
  return COOLANT_WARN_C > 0 && v.coolantKnown(nowMs) && v.coolantC >= COOLANT_WARN_C;
}

}  // namespace EngineData
