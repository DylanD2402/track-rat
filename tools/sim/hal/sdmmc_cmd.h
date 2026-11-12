// Stand-in (tools/build_sim.py), see driver/sdmmc_host.h
#pragma once
#include <stdint.h>
struct sdmmc_host_t;
struct sdmmc_card_t {
  struct {
    uint32_t capacity = 0, sector_size = 512;
  } csd;
};
inline int sdmmc_card_init(const sdmmc_host_t*, sdmmc_card_t*) { return 1; }
inline int sdmmc_read_sectors(sdmmc_card_t*, void*, size_t, size_t) { return 1; }
