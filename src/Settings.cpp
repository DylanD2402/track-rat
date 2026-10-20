/**
 * Track Rat - Settings implementation
 *
 * One NVS key per setting, so a setting added later doesn't disturb the
 * others and an older unit simply gets the default for a key it never
 * stored. The time zone keeps the key the Wi-Fi page used before settings
 * existed ("tz_min"), so a zone a phone has already set carries over.
 */

#include "Settings.h"
#include <Arduino.h>
#include <Preferences.h>
#include "AutoStart.h"

namespace Settings {

static SettingsData current = settings::defaults();
static volatile uint32_t changes = 0;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;  // current and changes
static SemaphoreHandle_t writeLock = nullptr;             // one set() at a time (loop or web server)
static Preferences prefs;
static bool open = false;

void begin() {
  writeLock = xSemaphoreCreateMutex();
  open = prefs.begin("trackrat", false);
  SettingsData d = settings::defaults();
  if (open) {
    d.setupDone = prefs.getBool("setup", d.setupDone);
    d.units = (SpeedUnits)prefs.getUChar("units", (uint8_t)d.units);
    d.rotation = prefs.getUChar("rot", d.rotation);
    d.brightness = prefs.getUChar("bright", d.brightness);
    d.compareTo = (CompareTo)prefs.getUChar("cmp", (uint8_t)d.compareTo);
    d.bigNumber = (BigNumber)prefs.getUChar("big", (uint8_t)d.bigNumber);
    d.deltaColours = (DeltaColours)prefs.getUChar("dcol", (uint8_t)d.deltaColours);
    d.sessionLength = prefs.getUChar("seslen", d.sessionLength);
    d.gpsMode = (GpsMode)prefs.getUChar("gps", (uint8_t)d.gpsMode);
    d.bluetooth = prefs.getBool("bt", d.bluetooth);
    d.utcOffsetMin = prefs.getShort("tz_min", d.utcOffsetMin);
    d.recordRoadDrives = prefs.getBool("road", d.recordRoadDrives);
  } else {
    DEBUG_PRINTLN("Settings: flash storage unavailable - using defaults, changes won't be kept");
  }
  settings::sanitize(d);
  portENTER_CRITICAL(&lock);
  current = d;
  portEXIT_CRITICAL(&lock);

  DEBUG_PRINTF("Settings: %s, screen %s, brightness %d%%, big number %s, GPS %s, Bluetooth %s, UTC%+d min, "
               "road drives %s%s\n",
               d.units == SpeedUnits::Mph ? "mph" : "km/h", settings::upsideDown(d) ? "upside down" : "normal",
               d.brightness, d.bigNumber == BigNumber::Delta ? "delta" : d.bigNumber == BigNumber::Predicted ? "predicted" : "lap time",
               d.gpsMode == GpsMode::Fast20 ? "20 Hz" : "10 Hz all systems", d.bluetooth ? "on" : "off", d.utcOffsetMin,
               d.recordRoadDrives ? "saved" : "not saved", d.setupDone ? "" : " (first boot: asking which way up and units)");
}

SettingsData get() {
  portENTER_CRITICAL(&lock);
  SettingsData d = current;
  portEXIT_CRITICAL(&lock);
  return d;
}

uint32_t changeCount() { return changes; }

void set(const SettingsData& s) {
  SettingsData n = s;
  settings::sanitize(n);
  if (writeLock) xSemaphoreTake(writeLock, portMAX_DELAY);
  const SettingsData o = get();
  if (n != o) {
    portENTER_CRITICAL(&lock);
    current = n;
    changes = changes + 1;
    portEXIT_CRITICAL(&lock);
    // Flash writes outside the spinlock: they take milliseconds
    if (open) {
      if (n.setupDone != o.setupDone) prefs.putBool("setup", n.setupDone);
      if (n.units != o.units) prefs.putUChar("units", (uint8_t)n.units);
      if (n.rotation != o.rotation) prefs.putUChar("rot", n.rotation);
      if (n.brightness != o.brightness) prefs.putUChar("bright", n.brightness);
      if (n.compareTo != o.compareTo) prefs.putUChar("cmp", (uint8_t)n.compareTo);
      if (n.bigNumber != o.bigNumber) prefs.putUChar("big", (uint8_t)n.bigNumber);
      if (n.deltaColours != o.deltaColours) prefs.putUChar("dcol", (uint8_t)n.deltaColours);
      if (n.sessionLength != o.sessionLength) prefs.putUChar("seslen", n.sessionLength);
      if (n.gpsMode != o.gpsMode) prefs.putUChar("gps", (uint8_t)n.gpsMode);
      if (n.bluetooth != o.bluetooth) prefs.putBool("bt", n.bluetooth);
      if (n.utcOffsetMin != o.utcOffsetMin) prefs.putShort("tz_min", n.utcOffsetMin);
      if (n.recordRoadDrives != o.recordRoadDrives) prefs.putBool("road", n.recordRoadDrives);
    }
  }
  if (writeLock) xSemaphoreGive(writeLock);
}

int nextSessionNumber(const std::string& trackId, const std::string& stamp) {
  if (writeLock) xSemaphoreTake(writeLock, portMAX_DELAY);
  SessionCount last;
  if (open) {
    last.trackId = prefs.getString("ses_trk", "").c_str();
    last.date = prefs.getString("ses_day", "").c_str();
    last.number = prefs.getInt("ses_num", 0);
  }
  int n = ::nextSessionNumber(last, trackId, stamp);
  if (open) {
    prefs.putString("ses_trk", last.trackId.c_str());
    prefs.putString("ses_day", last.date.c_str());
    prefs.putInt("ses_num", last.number);
  }
  if (writeLock) xSemaphoreGive(writeLock);
  return n;
}

}  // namespace Settings
