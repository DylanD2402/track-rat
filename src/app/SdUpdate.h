/**
 * Firmware update from the SD card: offers /trackrat.bin at boot, moved out of main.cpp.
 */
#ifndef SD_UPDATE_H
#define SD_UPDATE_H

namespace SdUpdate {

void offer();  // if /trackrat.bin is on the card: ask, install and restart; otherwise return

}  // namespace SdUpdate

#endif
