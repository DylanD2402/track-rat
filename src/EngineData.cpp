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

const carobd::Profile* carProfile() { return ObdBle::carProfile(); }

bool carValue(int i, uint32_t nowMs, float& v) {
  const carobd::Profile* p = carProfile();
  if (!p || i < 0 || i >= p->nSigs) return false;
  const int c = p->sigs[i].command;
  const uint32_t period = c >= 0 && c < p->nCmds ? p->cmds[c].periodMs : 1000;
  const carobd::Values& vals = ObdBle::carValues();
  if (!vals.known(i, nowMs, period * 3 > 1000 ? period * 3 : 1000)) return false;
  v = vals.v[i];
  return true;
}

bool oil(uint32_t nowMs, float& c) {
  const obd::Values& v = values();
  if (!v.oilKnown(nowMs)) return false;
  c = v.oilC;
  return true;
}

}  // namespace EngineData
