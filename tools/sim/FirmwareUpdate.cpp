// Stand-in for src/FirmwareUpdate.cpp (tools/build_sim.py): the SD card never holds a firmware file, so the
// boot offer to install one never shows. Same header (include/FirmwareUpdate.h).
#include "FirmwareUpdate.h"

namespace FirmwareUpdate {
size_t slotSize() { return 6553600; }
bool find(Offer&) { return false; }
const char* install(const Offer&, void (*)(size_t, size_t)) { return "Not available in the browser"; }
}  // namespace FirmwareUpdate
