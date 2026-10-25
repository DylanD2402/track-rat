/**
 * Track Rat - a car profile's JSON (see CarProfiles.h) into a carobd::Profile
 *
 * Only ArduinoJson, no SD card or Arduino code, so it can be tried on a PC
 * (ArduinoJson is header-only).
 */

#ifndef CAR_PROFILE_JSON_H
#define CAR_PROFILE_JSON_H

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include "CarObd.h"
#include "Config.h"

namespace CarProfiles {

inline bool sameName(const char* a, const char* b) {  // ignoring case
  for (; *a && *b; a++, b++) {
    const char x = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a, y = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
    if (x != y) return false;
  }
  return *a == *b;
}

inline void readFormat(JsonObjectConst f, carobd::Format& out) {
  out = carobd::Format();
  out.bix = f["bix"] | 0;
  out.len = f["len"] | 8;
  out.sign = f["sign"] | false;
  out.lsbFirst = f["blsb"] | false;
  out.mul = f["mul"] | 1.0f;
  out.div = f["div"] | 1.0f;
  out.add = f["add"] | 0.0f;
  if (!f["min"].isNull()) out.hasMin = true, out.min = f["min"];
  if (!f["max"].isNull()) out.hasMax = true, out.max = f["max"];
  if (!f["nullmin"].isNull()) out.hasNullMin = true, out.nullMin = f["nullmin"];
  if (!f["nullmax"].isNull()) out.hasNullMax = true, out.nullMax = f["nullmax"];
}

// One profile from the JSON in doc; name: the file's, if the profile has none.
inline bool readProfile(JsonDocument& doc, const char* fileName, carobd::Profile& p) {
  p = carobd::Profile();
  snprintf(p.name, sizeof(p.name), "%s", (const char*)(doc["name"] | fileName));
  snprintf(p.vin, sizeof(p.vin), "%s", (const char*)(doc["vin"] | ""));
  p.year = doc["year"] | 0;
  int skipped = 0;
  for (JsonObjectConst c : doc["commands"].as<JsonArrayConst>()) {
    if (p.nCmds >= carobd::MAX_COMMANDS || p.nSigs >= carobd::MAX_SIGNALS) {
      skipped++;
      continue;
    }
    carobd::Command& cmd = p.cmds[p.nCmds];
    cmd = carobd::Command();
    JsonObjectConst req = c["cmd"];
    if (req.isNull() || req.size() != 1 ||
        !carobd::setModule(cmd, c["hdr"] | "", c["rax"] | "", c["eax"] | "", c["pri"] | "") ||
        !carobd::setRequest(cmd, req.begin()->key().c_str(), req.begin()->value() | "")) {
      DEBUG_PRINTF("Car: %s: a command it can't read (hdr %s) - skipped\n", fileName, (const char*)(c["hdr"] | "?"));
      continue;
    }
    cmd.fcm1 = c["fcm1"] | false;
    cmd.periodMs = carobd::periodFromSeconds(c["freq"] | 1.0);
    int got = 0;
    for (JsonObjectConst s : c["signals"].as<JsonArrayConst>()) {
      if (p.nSigs >= carobd::MAX_SIGNALS) {
        skipped++;
        continue;
      }
      carobd::Signal& sig = p.sigs[p.nSigs];
      sig = carobd::Signal();
      snprintf(sig.id, sizeof(sig.id), "%s", (const char*)(s["id"] | "?"));
      snprintf(sig.name, sizeof(sig.name), "%s", (const char*)(s["name"] | sig.id));
      snprintf(sig.unit, sizeof(sig.unit), "%s", carobd::unitText(s["fmt"]["unit"] | ""));
      readFormat(s["fmt"], sig.fmt);
      sig.command = (int8_t)p.nCmds;
      // The name of one of the log's own channels: the car's is told apart
      static const char* const OWN[] = {"satellites", "time", "latitude", "longitude", "velocity kmh", "heading", "height",
                                        "HDOP", "Lap", "Lateral G", "Longitudinal G", "Lap tag", "RPM", "Throttle position",
                                        "Coolant temperature", "Intake temperature", "Oil temperature"};
      for (const char* own : OWN)
        if (sameName(own, sig.name)) {
          char n[sizeof(sig.name)];
          snprintf(n, sizeof(n), "%.32s (car)", sig.name);
          memcpy(sig.name, n, sizeof(n));
          break;
        }
      // Two signals with one name (the same value from two modules): the log needs them apart
      for (int copy = 2; copy <= carobd::MAX_SIGNALS; copy++) {  // "Brake pressure 2", then 3...
        bool taken = false;
        for (int k = 0; k < p.nSigs; k++) taken |= !strcmp(p.sigs[k].name, sig.name);
        if (!taken) break;
        char n[sizeof(sig.name)];
        const char* sp = strrchr(sig.name, ' ');
        const int keep = copy > 2 && sp ? (int)(sp - sig.name) : (int)strlen(sig.name);  // drop the last number
        snprintf(n, sizeof(n), "%.*s %d", keep < 36 ? keep : 36, sig.name, copy);
        memcpy(sig.name, n, sizeof(n));
      }
      p.nSigs++;
      got++;
    }
    if (got) p.nCmds++;
  }
  if (skipped)
    DEBUG_PRINTF("Car: %s has more than %d signals: kept the first ones (tools/obd_car.py makes a profile with just yours)\n",
                 fileName, carobd::MAX_SIGNALS);
  return p.nSigs > 0;
}

}  // namespace CarProfiles

#endif  // CAR_PROFILE_JSON_H
