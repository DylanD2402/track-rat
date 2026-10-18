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

// Try 4-bit, then 1-bit, then 1-bit slowly (marginal cards or wiring).
static bool mountSd() {
  struct Attempt { bool oneBit; int khz; };
  const Attempt attempts[] = {{false, SDMMC_FREQ_DEFAULT}, {true, SDMMC_FREQ_DEFAULT}, {true, 5000}};
  for (const Attempt& a : attempts) {
    if (a.oneBit) SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
    else SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
    if (SD_MMC.begin("/sdcard", a.oneBit, false, a.khz)) {
      DEBUG_PRINTF("Storage: SD card mounted (%llu MB, %s, %d kHz)\n", SD_MMC.cardSize() / (1024ULL * 1024ULL),
                   a.oneBit ? "1-bit" : "4-bit", a.khz);
      return true;
    }
    SD_MMC.end();
  }
  return false;
}

void begin() {
  sdOk = mountSd();
  if (!sdOk) {
    DEBUG_PRINTLN("Storage: SD card not mounted. If one is inserted it must be FAT32 (exFAT is not supported)");
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
