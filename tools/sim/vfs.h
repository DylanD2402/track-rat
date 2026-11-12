// The simulated unit's saved data (tools/sim/vfs.cpp)
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace sim {
double savedSeq();  // changes whenever saved data (settings, the SD card) changed
std::vector<uint8_t> exportSaved();
bool importSaved(const uint8_t* data, size_t len);
void putFlashFile(const char* path, const uint8_t* data, size_t len);
}  // namespace sim
