/**
 * Track Rat - Storage Implementation
 */

#include "Storage.h"
#include "Config.h"
#include <LittleFS.h>
#include <SD_MMC.h>

namespace Storage {

static bool sdOk = false;
static bool flashOk = false;

void begin() {
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  sdOk = SD_MMC.begin("/sdcard", false /* 4-bit */);
  if (!sdOk) {
    // Some cards/boards are only reliable in 1-bit mode
    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
    sdOk = SD_MMC.begin("/sdcard", true /* 1-bit */);
  }
  if (sdOk) {
    DEBUG_PRINTF("Storage: SD card mounted (%llu MB)\n", SD_MMC.cardSize() / (1024ULL * 1024ULL));
  } else {
    DEBUG_PRINTLN("Storage: no SD card");
  }

  flashOk = LittleFS.begin(true);  // formats an empty partition on first boot
  DEBUG_PRINTF("Storage: LittleFS %s\n", flashOk ? "mounted" : "FAILED");
}

bool sdMounted() { return sdOk; }
bool flashMounted() { return flashOk; }

fs::FS* sessionFS() {
  if (sdOk) return &SD_MMC;
  if (flashOk) return &LittleFS;
  return nullptr;
}

const char* sessionFSName() {
  if (sdOk) return "SD";
  if (flashOk) return "flash";
  return "none";
}

}  // namespace Storage
