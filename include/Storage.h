/**
 * Track Rat - Storage
 *
 * Mounts the SD card (SDIO 4-bit) and the internal LittleFS partition.
 * Sessions are written to the SD card when present, otherwise to flash.
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <FS.h>

namespace Storage {

void begin();
bool sdMounted();
bool flashMounted();

// Where session logs go: SD if mounted, else LittleFS, else nullptr.
fs::FS* sessionFS();
const char* sessionFSName();

}  // namespace Storage

#endif  // STORAGE_H
