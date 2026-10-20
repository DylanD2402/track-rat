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

bool coolant(uint32_t nowMs, float& c) {
  const obd::Values& v = values();
  if (!v.coolantKnown(nowMs)) return false;
  c = v.coolantC;
  return true;
}

bool oil(uint32_t nowMs, float& c) {
  const obd::Values& v = values();
  if (!v.oilKnown(nowMs)) return false;
  c = v.oilC;
  return true;
}

}  // namespace EngineData
