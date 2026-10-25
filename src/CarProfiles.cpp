/**
 * Track Rat - car profiles on the SD card (see CarProfiles.h)
 */

#include "CarProfiles.h"
#include <ArduinoJson.h>
#include "CarProfileJson.h"
#include <SD_MMC.h>
#include "Config.h"
#include "Storage.h"

namespace CarProfiles {

int load(carobd::Profile* out, int max) {
  if (!Storage::sdMounted() || !SD_MMC.exists(DIR)) return 0;
  File dir = SD_MMC.open(DIR);
  if (!dir || !dir.isDirectory()) return 0;
  int n = 0;
  for (File f = dir.openNextFile(); f && n < max; f = dir.openNextFile()) {
    String name = f.name();
    name = name.substring(name.lastIndexOf('/') + 1);
    if (f.isDirectory() || !name.endsWith(".json")) {
      f.close();
      continue;
    }
    DynamicJsonDocument doc(f.size() * 2 + 4096);  // large: in PSRAM
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      DEBUG_PRINTF("Car: %s/%s isn't valid JSON (%s) - skipped\n", DIR, name.c_str(), err.c_str());
      continue;
    }
    String base = name.substring(0, name.length() - 5);
    if (!readProfile(doc, base.c_str(), out[n])) {
      DEBUG_PRINTF("Car: %s/%s has no signals Track Rat can read - skipped\n", DIR, name.c_str());
      continue;
    }
    DEBUG_PRINTF("Car: profile '%s' (%s%s, %d signals, %d requests)\n", out[n].name, out[n].vin[0] ? "VIN " : "any car",
                 out[n].vin, out[n].nSigs, out[n].nCmds);
    n++;
  }
  dir.close();
  return n;
}

}  // namespace CarProfiles
