// Stand-in (tools/build_sim.py), see driver/sdmmc_host.h. FatFs is not there: ff_diskio_get_pdrv_card says so,
// and Storage::DirLister lists a folder through the File API, as it does on flash.
#pragma once
#include <stdint.h>
typedef uint8_t BYTE;
#define FF_VOLUMES 2
#define AM_DIR 0x10
#define FR_OK 0
struct FF_DIR { int unused; };
struct FILINFO {
  char fname[256];
  uint32_t fsize;
  uint8_t fattrib;
};
inline int f_opendir(FF_DIR*, const char*) { return 1; }
inline int f_readdir(FF_DIR*, FILINFO*) { return 1; }
inline int f_closedir(FF_DIR*) { return 0; }
