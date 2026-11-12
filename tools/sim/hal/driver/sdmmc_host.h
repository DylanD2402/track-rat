// Stand-in (tools/build_sim.py): just enough of the SD controller's names for Storage.cpp to compile. The SD card
// is an in-memory volume that mounts at the first try, so none of these is ever called.
#pragma once
#include <stdint.h>
#include <esp_heap_caps.h>
#include <sdmmc_cmd.h>

typedef int esp_err_t;
#define ESP_OK 0
typedef int gpio_num_t;

#define SDMMC_FREQ_DEFAULT 20000
#define SDMMC_HOST_FLAG_1BIT 1
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1

struct sdmmc_host_t {
  uint32_t flags = 0;
  int slot = 0;
  int max_freq_khz = 0;
};
struct sdmmc_slot_config_t {
  int width = 4;
  gpio_num_t clk = 0, cmd = 0, d0 = 0;
  uint32_t flags = 0;
};
#define SDMMC_HOST_DEFAULT() (sdmmc_host_t())
#define SDMMC_SLOT_CONFIG_DEFAULT() (sdmmc_slot_config_t())

inline esp_err_t sdmmc_host_init() { return 1; }
inline esp_err_t sdmmc_host_init_slot(int, const sdmmc_slot_config_t*) { return 1; }
inline esp_err_t sdmmc_host_deinit() { return 0; }
