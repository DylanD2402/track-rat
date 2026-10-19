/**
 * Track Rat - Firmware update from the SD card
 *
 * If /trackrat.bin is on the card at boot, main.cpp offers to install it.
 * It is written to the other app slot with the Update library, then renamed
 * so it isn't offered again.
 */

#ifndef FIRMWARE_UPDATE_H
#define FIRMWARE_UPDATE_H

#include <Arduino.h>
#include "FirmwareImage.h"

namespace FirmwareUpdate {

struct Offer {
  size_t size = 0;
  firmware::Check check = firmware::Check::TooShort;  // Ok = safe to install
  time_t modified = 0;                                // the file's date on the card
};

// How much room the app slot we'd write to has (0 if there is none). The Wi-Fi upload uses it too.
size_t slotSize();

// Is there a firmware file on the SD card? Fills in what it is.
bool find(Offer& offer);

// Writes the file to the other app slot. progress(done, total) is called as it goes.
// Returns nullptr on success (the file has then been renamed; reboot next), otherwise what went wrong.
const char* install(const Offer& offer, void (*progress)(size_t done, size_t total));

}  // namespace FirmwareUpdate

#endif  // FIRMWARE_UPDATE_H
