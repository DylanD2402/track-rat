// Stand-in (tools/build_sim.py), see ff.h
#pragma once
#include <sdmmc_cmd.h>
inline BYTE ff_diskio_get_pdrv_card(const sdmmc_card_t*) { return 0xFF; }
