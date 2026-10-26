/**
 * Track Rat - Storage Implementation
 */

#include "Storage.h"
#include "Config.h"
#include <LittleFS.h>
#include <SD_MMC.h>
#include <memory>
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "sdmmc_cmd.h"
#include "ff.h"
#include "diskio_sdmmc.h"  // after ff.h, whose BYTE it uses

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

// What a boot sector's name/type fields say the file system is.
static const char* bootSectorType(const uint8_t* b) {
  if (memcmp(b + 3, "EXFAT   ", 8) == 0) return "exFAT";
  if (memcmp(b + 3, "NTFS    ", 8) == 0) return "NTFS";
  if (memcmp(b + 0x52, "FAT32   ", 8) == 0) return "FAT32";
  if (memcmp(b + 0x36, "FAT", 3) == 0) return "FAT12/16";
  return "unknown";
}

// Mounting failed: read the card directly and log why.
static void diagnoseSd() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  host.max_freq_khz = 5000;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk = (gpio_num_t)SD_CLK;
  slot.cmd = (gpio_num_t)SD_CMD;
  slot.d0 = (gpio_num_t)SD_D0;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  sdmmc_card_t card;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(512, MALLOC_CAP_DMA);
  if (!buf || sdmmc_host_init() != ESP_OK || sdmmc_host_init_slot(host.slot, &slot) != ESP_OK) {
    DEBUG_PRINTLN("SD check: could not start the SD controller");
  } else if (sdmmc_card_init(&host, &card) != ESP_OK) {
    DEBUG_PRINTLN("SD check: no card answering - is it pushed in until it clicks?");
  } else if (sdmmc_read_sectors(&card, buf, 0, 1) != ESP_OK) {
    DEBUG_PRINTLN("SD check: card found but sector 0 can't be read - try another card");
  } else {
    DEBUG_PRINTF("SD check: card found, %llu MB\n",
                 (uint64_t)card.csd.capacity * card.csd.sector_size / (1024ULL * 1024ULL));
    if (buf[510] != 0x55 || buf[511] != 0xAA) {
      DEBUG_PRINTLN("SD check: no partition table or boot sector - reformat the card as FAT32");
    } else if (buf[0x1C2] == 0xEE) {
      DEBUG_PRINTLN("SD check: card uses GPT partitioning - reformat with an MBR partition, FAT32");
    } else if (buf[0] == 0xEB || buf[0] == 0xE9) {
      DEBUG_PRINTF("SD check: no partition table, file system is %s%s\n", bootSectorType(buf),
                   strcmp(bootSectorType(buf), "FAT32") ? " - reformat as FAT32" : "");
    } else {
      uint8_t type = buf[0x1C2];
      uint32_t lba = buf[0x1C6] | buf[0x1C7] << 8 | buf[0x1C8] << 16 | (uint32_t)buf[0x1C9] << 24;
      const char* fs = "?";
      if (sdmmc_read_sectors(&card, buf, lba, 1) == ESP_OK) fs = bootSectorType(buf);
      DEBUG_PRINTF("SD check: partition 1 type 0x%02X at sector %lu, file system %s%s\n", type,
                   (unsigned long)lba, fs, strcmp(fs, "FAT32") ? " - reformat as FAT32" : "");
    }
  }
  sdmmc_host_deinit();
  free(buf);
}

void begin() {
  sdOk = mountSd();
  if (!sdOk) {
    DEBUG_PRINTLN("Storage: SD card not mounted");
    diagnoseSd();
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

// --- DirLister ---------------------------------------------------------------

// SD_MMC keeps the card it mounted to itself (protected). Naming the member
// through a derived class is allowed when taking its address.
struct SdMmcCard : fs::SDMMCFS {
  static sdmmc_card_t* of(fs::SDMMCFS& sd) { return sd.*(&SdMmcCard::_card); }
};

// FatFs locks the volume for each call (FF_FS_REENTRANT), so the log writer can
// write meanwhile: a file made or renamed during the listing may or may not show.
struct DirLister::State {
  bool fat = false;
  FF_DIR fatDir;
  FILINFO info;
  File dir;
};

DirLister::DirLister() {}
DirLister::~DirLister() { close(); }

bool DirLister::open(fs::FS& fs, const char* path) {
  close();
  std::unique_ptr<State> s(new State());
  if (&fs == &SD_MMC && sdOk) {
    sdmmc_card_t* card = SdMmcCard::of(SD_MMC);
    BYTE drive = card ? ff_diskio_get_pdrv_card(card) : 0xFF;
    if (drive < FF_VOLUMES) {
      char full[48];
      snprintf(full, sizeof(full), "%u:%s", (unsigned)drive, path);
      if (f_opendir(&s->fatDir, full) == FR_OK) s->fat = true;
      else return false;  // no such folder
    }
  }
  if (!s->fat) {
    s->dir = fs.open(path);
    if (!s->dir || !s->dir.isDirectory()) return false;
  }
  state = s.release();
  return true;
}

bool DirLister::next(Entry& out) {
  if (!state) return false;
  if (state->fat) {
    // "." and ".." aren't returned by FatFs's f_readdir for FAT32 roots; skip them anyway
    while (f_readdir(&state->fatDir, &state->info) == FR_OK && state->info.fname[0]) {
      if (!strcmp(state->info.fname, ".") || !strcmp(state->info.fname, "..")) continue;
      out.name = state->info.fname;
      out.size = (uint32_t)state->info.fsize;
      out.isDir = (state->info.fattrib & AM_DIR) != 0;
      return true;
    }
  } else if (File e = state->dir.openNextFile()) {
    std::string name = e.name();  // the name alone or the whole path, depending on the core
    const size_t slash = name.rfind('/');
    out.name = slash == std::string::npos ? name : name.substr(slash + 1);
    out.size = e.size();
    out.isDir = e.isDirectory();
    e.close();
    return true;
  }
  close();
  return false;
}

void DirLister::close() {
  if (!state) return;
  if (state->fat) f_closedir(&state->fatDir);
  state->dir.close();
  delete state;
  state = nullptr;
}

}  // namespace Storage
