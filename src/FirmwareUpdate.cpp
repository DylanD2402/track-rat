/**
 * Track Rat - Firmware update implementation
 */

#include "FirmwareUpdate.h"
#include "Config.h"
#include "Storage.h"
#include <SD_MMC.h>
#include <Update.h>
#include "esp_ota_ops.h"

namespace FirmwareUpdate {

// How much room the slot we'd write to has.
static size_t slotSize() {
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return p ? p->size : 0;
}

bool find(Offer& offer) {
  offer = Offer();
  if (!Storage::sdMounted() || !SD_MMC.exists(FIRMWARE_FILE)) return false;

  File f = SD_MMC.open(FIRMWARE_FILE, FILE_READ);
  if (!f) return false;
  uint8_t head[firmware::HEADER_BYTES];
  size_t got = f.read(head, sizeof(head));
  offer.size = f.size();
  offer.check = firmware::check(head, got, offer.size, slotSize());
  offer.modified = f.getLastWrite();
  f.close();
  DEBUG_PRINTF("Firmware: %s on the SD card, %u bytes: %s\n", FIRMWARE_FILE, (unsigned)offer.size,
               firmware::describe(offer.check));
  return true;
}

// Move the installed file out of the way so it isn't offered again.
static bool retire() {
  SD_MMC.remove(FIRMWARE_DONE_FILE);  // an older one may be there
  if (SD_MMC.rename(FIRMWARE_FILE, FIRMWARE_DONE_FILE)) return true;
  return SD_MMC.remove(FIRMWARE_FILE);
}

const char* install(const Offer& offer, void (*progress)(size_t done, size_t total)) {
  if (offer.check != firmware::Check::Ok) return firmware::describe(offer.check);

  File f = SD_MMC.open(FIRMWARE_FILE, FILE_READ);
  if (!f) return "can't open the file";

  if (!Update.begin(offer.size, U_FLASH)) {
    f.close();
    DEBUG_PRINTF("Firmware: Update.begin failed: %s\n", Update.errorString());
    return "no room in the app slot";
  }

  uint8_t* buf = (uint8_t*)malloc(4096);
  if (!buf) {
    Update.abort();
    f.close();
    return "out of memory";
  }

  firmware::Copy copied = firmware::copyImage(
      offer.size, buf, 4096, [&](uint8_t* b, size_t want) { return f.read(b, want); },
      [&](const uint8_t* b, size_t n) { return Update.write((uint8_t*)b, n) == n; },
      [&](size_t done, size_t total) {
        if (progress) progress(done, total);
      });
  free(buf);
  f.close();

  if (copied != firmware::Copy::Ok) {
    DEBUG_PRINTF("Firmware: copy failed: %s\n", Update.errorString());
    Update.abort();
    return copied == firmware::Copy::ReadError ? "read error: is the card still in?" : "flash write failed";
  }
  if (!Update.end(true) || !Update.isFinished()) {  // checks the image, then makes the new slot the boot slot
    DEBUG_PRINTF("Firmware: Update.end failed: %s\n", Update.errorString());
    return "the new firmware didn't check out";
  }

  DEBUG_PRINTF("Firmware: installed %u bytes\n", (unsigned)offer.size);
  if (!retire()) DEBUG_PRINTLN("Firmware: couldn't rename or delete the file - it will be offered again");
  return nullptr;
}

}  // namespace FirmwareUpdate
