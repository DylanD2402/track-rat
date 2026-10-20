/**
 * Track Rat - Settings (kept in flash)
 *
 * Settings::get() is the current settings for any code to read. They are
 * loaded once at boot (Settings::begin) and written to flash (NVS, through
 * Preferences) when set() changes them. The struct, defaults and rules are
 * in SettingsData.h.
 *
 * The Wi-Fi page's web server calls set() from its own task, so get() hands
 * out a copy and set() is safe from either core. Whatever has to follow a
 * change (screen rotation, backlight, Bluetooth) is done by main.cpp's loop,
 * which watches changeCount().
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>
#include "SettingsData.h"

namespace Settings {

void begin();  // load from flash; call before anything that reads get()
SettingsData get();
void set(const SettingsData& s);  // validate, use, and save what changed
uint32_t changeCount();           // goes up by one with every change

// "Session N" for a session starting now at trackId (see nextSessionNumber
// in AutoStart.h). The count is kept in flash with the settings, so a
// session after a power cut is still the next one of the day.
int nextSessionNumber(const std::string& trackId, const std::string& stamp);

// A speed in km/h as the driver wants to see it, and its unit.
inline float speed(float kmh) { return settings::speedValue(kmh, get().units); }
inline const char* speedUnit() { return settings::speedUnitLabel(get().units); }

}  // namespace Settings

#endif  // SETTINGS_H
