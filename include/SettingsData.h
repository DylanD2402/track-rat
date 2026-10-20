/**
 * Track Rat - Settings data and rules
 *
 * The settings struct, its defaults (from Config.h), validation, how each
 * item steps and reads on screen, the names the Wi-Fi page uses, and the
 * small conversions that depend on a setting. Pure C++ with no Arduino code,
 * so it is tested on the PC (test/test_settings.cpp). Storing it in flash is
 * Settings.cpp.
 */

#ifndef SETTINGS_DATA_H
#define SETTINGS_DATA_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Config.h"

enum class SpeedUnits : uint8_t { Kmh = 0, Mph = 1 };
enum class CompareTo : uint8_t { Day = 0, Ever = 1, Session = 2 };       // what the live delta compares with
enum class BigNumber : uint8_t { Delta = 0, Predicted = 1, LapTime = 2 };  // the largest number on the timing screen
enum class GpsMode : uint8_t { Fast20 = 0, AllSystems10 = 1 };
enum class DeltaColours : uint8_t { GreenRed = 0, BlueOrange = 1 };  // ahead / behind on the timing screen and LED bar

struct SettingsData {
  bool setupDone;          // false until the first-boot questions are answered
  SpeedUnits units;
  uint8_t rotation;        // 1 or 3 (DISPLAY_ROTATION is "right way up", the other is upside down)
  uint8_t brightness;      // percent, 10..100
  CompareTo compareTo;     // the live delta's reference from the first lap (BestLap.h)
  BigNumber bigNumber;
  DeltaColours deltaColours;
  uint8_t sessionLength;   // 0 = off, 1..3 = 20, 25, 30 minutes (SituationScreen.h): the header counts down
  GpsMode gpsMode;         // applied when the GPS is set up at boot
  bool bluetooth;          // RaceBox Bluetooth output
  int16_t utcOffsetMin;    // minutes east of UTC, -720..840 (the Wi-Fi page sets it from the phone)
  bool recordRoadDrives;   // Free Roam drives are saved to the card
  bool kartEverywhere;     // every track is timed as a kart track (else only "kind": "kart" in tracks.json)
  uint8_t minLap;          // the shortest lap: 0 = auto (MIN_LAP_TIME_MS, KART_MIN_LAP_MS for karts), else MIN_LAP_S[minLap] s
  bool cameraWithSessions; // a paired DJI camera records with the sessions (6.2); else only from its own button
};

inline bool operator==(const SettingsData& a, const SettingsData& b) {
  return a.setupDone == b.setupDone && a.units == b.units && a.rotation == b.rotation && a.brightness == b.brightness &&
         a.compareTo == b.compareTo && a.bigNumber == b.bigNumber && a.deltaColours == b.deltaColours && a.sessionLength == b.sessionLength &&
         a.gpsMode == b.gpsMode &&
         a.bluetooth == b.bluetooth && a.utcOffsetMin == b.utcOffsetMin && a.recordRoadDrives == b.recordRoadDrives &&
         a.kartEverywhere == b.kartEverywhere && a.minLap == b.minLap && a.cameraWithSessions == b.cameraWithSessions;
}
inline bool operator!=(const SettingsData& a, const SettingsData& b) { return !(a == b); }

// The items on the settings screen, in the order they appear.
enum class SettingItem : uint8_t { Units, Upside, Brightness, Compare, Big, Colours, SessionLen, Gps, Bluetooth, TimeZone, RoadDrives,
                                   MinLap, Kart, CameraRec, Count,
                                   Engine, Camera };  // after Count: rows that open a screen of their own, not values
constexpr int SETTING_ITEM_COUNT = (int)SettingItem::Count;

namespace settings {

constexpr int BRIGHTNESS_MIN = 10, BRIGHTNESS_MAX = 100, BRIGHTNESS_STEP = 10;
constexpr int UTC_MIN = -12 * 60, UTC_MAX = 14 * 60, UTC_STEP = 30;
constexpr float KMH_PER_MPH = 1.609344f;
// The Min lap time choices, in seconds (0 = auto). Four: a choice page holds four.
constexpr uint8_t MIN_LAP_S[] = {0, 10, 15, 30};
constexpr int MIN_LAP_CHOICES = sizeof(MIN_LAP_S);

inline SettingsData defaults() {
  SettingsData d;
  d.setupDone = false;
  d.units = DEFAULT_SPEED_MPH ? SpeedUnits::Mph : SpeedUnits::Kmh;
  d.rotation = DISPLAY_ROTATION == 3 ? 3 : 1;
  d.brightness = DEFAULT_BRIGHTNESS_PCT;
  d.compareTo = (CompareTo)DEFAULT_COMPARE_TO;
  d.bigNumber = (BigNumber)DEFAULT_BIG_NUMBER;
  d.deltaColours = (DeltaColours)DEFAULT_DELTA_COLOURS;
  d.sessionLength = DEFAULT_SESSION_LENGTH;
  d.gpsMode = GPS_TARGET_RATE_HZ >= 20 ? GpsMode::Fast20 : GpsMode::AllSystems10;
  d.bluetooth = BLE_ENABLED != 0;
  d.utcOffsetMin = DEFAULT_UTC_OFFSET_MIN;
  d.recordRoadDrives = DEFAULT_RECORD_ROAD_DRIVES != 0;
  d.kartEverywhere = false;
  d.minLap = 0;
  d.cameraWithSessions = true;
  return d;
}

// Put anything out of range (a corrupt value in flash) back to something
// valid: the default for a choice, the nearest limit for a number.
inline void sanitize(SettingsData& d) {
  const SettingsData def = defaults();
  if ((uint8_t)d.units > (uint8_t)SpeedUnits::Mph) d.units = def.units;
  if (d.rotation != 1 && d.rotation != 3) d.rotation = def.rotation;
  if (d.brightness < BRIGHTNESS_MIN) d.brightness = BRIGHTNESS_MIN;
  if (d.brightness > BRIGHTNESS_MAX) d.brightness = BRIGHTNESS_MAX;
  if ((uint8_t)d.compareTo > (uint8_t)CompareTo::Session) d.compareTo = def.compareTo;
  if ((uint8_t)d.bigNumber > (uint8_t)BigNumber::LapTime) d.bigNumber = def.bigNumber;
  if ((uint8_t)d.deltaColours > (uint8_t)DeltaColours::BlueOrange) d.deltaColours = def.deltaColours;
  if (d.sessionLength > 3) d.sessionLength = def.sessionLength;
  if ((uint8_t)d.gpsMode > (uint8_t)GpsMode::AllSystems10) d.gpsMode = def.gpsMode;
  if (d.minLap >= MIN_LAP_CHOICES) d.minLap = def.minLap;
  if (d.utcOffsetMin < UTC_MIN) d.utcOffsetMin = UTC_MIN;
  if (d.utcOffsetMin > UTC_MAX) d.utcOffsetMin = UTC_MAX;
  d.utcOffsetMin = (int16_t)(((d.utcOffsetMin + (d.utcOffsetMin >= 0 ? 7 : -7)) / 15) * 15);  // whole quarter hours
}

inline uint8_t cycle(uint8_t v, int count, int dir) { return (uint8_t)((v + dir % count + count) % count); }

// Step one item by dir (-1 or +1). Choices with two values just flip. For
// the others, wrap = true goes round (the BOOT button, which only has "+"),
// wrap = false stops at the ends (the touch "-" and "+").
inline void adjust(SettingsData& d, SettingItem item, int dir, bool wrap) {
  auto choice = [&](uint8_t v, int count) {
    if (count == 2 || wrap) return cycle(v, count, dir);
    int n = (int)v + dir;
    return (uint8_t)(n < 0 ? 0 : n > count - 1 ? count - 1 : n);
  };
  auto number = [&](int v, int lo, int hi, int step) {
    v += dir * step;
    if (v > hi) v = wrap ? lo : hi;
    if (v < lo) v = wrap ? hi : lo;
    return v;
  };
  switch (item) {
    case SettingItem::Units: d.units = (SpeedUnits)choice((uint8_t)d.units, 2); break;
    case SettingItem::Upside: d.rotation = d.rotation == 1 ? 3 : 1; break;
    case SettingItem::Brightness: d.brightness = (uint8_t)number(d.brightness, BRIGHTNESS_MIN, BRIGHTNESS_MAX, BRIGHTNESS_STEP); break;
    case SettingItem::Compare: d.compareTo = (CompareTo)choice((uint8_t)d.compareTo, 3); break;
    case SettingItem::Big: d.bigNumber = (BigNumber)choice((uint8_t)d.bigNumber, 3); break;
    case SettingItem::Colours: d.deltaColours = (DeltaColours)choice((uint8_t)d.deltaColours, 2); break;
    case SettingItem::SessionLen: d.sessionLength = choice(d.sessionLength, 4); break;
    case SettingItem::Gps: d.gpsMode = (GpsMode)choice((uint8_t)d.gpsMode, 2); break;
    case SettingItem::Bluetooth: d.bluetooth = !d.bluetooth; break;
    case SettingItem::TimeZone: d.utcOffsetMin = (int16_t)number(d.utcOffsetMin, UTC_MIN, UTC_MAX, UTC_STEP); break;
    case SettingItem::RoadDrives: d.recordRoadDrives = !d.recordRoadDrives; break;
    case SettingItem::MinLap: d.minLap = choice(d.minLap, MIN_LAP_CHOICES); break;
    case SettingItem::Kart: d.kartEverywhere = !d.kartEverywhere; break;
    case SettingItem::CameraRec: d.cameraWithSessions = !d.cameraWithSessions; break;
    default: break;
  }
}

inline bool upsideDown(const SettingsData& d) { return d.rotation != DISPLAY_ROTATION; }

// A change that only takes effect after a restart (the GPS is set up at boot).
inline bool needsRestart(const SettingsData& before, const SettingsData& after) {
  return before.gpsMode != after.gpsMode || before.kartEverywhere != after.kartEverywhere;  // the track list reads it at boot
}

inline const char* itemName(SettingItem item) {
  switch (item) {
    case SettingItem::Units: return "SPEED UNITS";
    case SettingItem::Upside: return "SCREEN";
    case SettingItem::Brightness: return "BRIGHTNESS";
    case SettingItem::Compare: return "COMPARE WITH";
    case SettingItem::Big: return "BIG NUMBER";
    case SettingItem::Colours: return "DELTA COLOURS";
    case SettingItem::SessionLen: return "SESSION LENGTH";
    case SettingItem::Gps: return "GPS MODE";
    case SettingItem::Bluetooth: return "BLUETOOTH";
    case SettingItem::TimeZone: return "TIME ZONE";
    case SettingItem::RoadDrives: return "RECORD ROAD DRIVES";
    case SettingItem::MinLap: return "MIN LAP TIME";
    case SettingItem::Kart: return "KART EVERYWHERE";
    case SettingItem::CameraRec: return "CAMERA RECORDING";
    case SettingItem::Engine: return "ENGINE DATA";
    case SettingItem::Camera: return "CAMERA";
    default: return "";
  }
}

// One line under the value, for items that need a word of explanation.
inline const char* itemNote(SettingItem item) {
  switch (item) {
    case SettingItem::Units: return "Every speed on screen and on the Wi-Fi page";
    case SettingItem::Upside: return "For mounting the unit upside down";
    case SettingItem::Compare: return "Saved. Only this session's best exists so far";
    case SettingItem::Big: return "The largest number on the timing screen";
    case SettingItem::Colours: return "Ahead and behind. Blue and orange for colour blindness";
    case SettingItem::SessionLen: return "The header counts down the time left";
    case SettingItem::Gps: return "10 Hz uses all four satellite systems. Restart to apply";
    case SettingItem::Bluetooth: return "RaceBox output for phone apps";
    case SettingItem::TimeZone: return "For file names. The Wi-Fi page sets it from your phone";
    case SettingItem::RoadDrives: return "Off: Free Roam drives are shown but not saved";
    case SettingItem::MinLap: return "Line crossings sooner are ignored. Auto: 20 s, karts 10 s";
    case SettingItem::Kart: return "8 m lines, karts' min lap everywhere. Restart to apply";
    case SettingItem::CameraRec: return "A paired DJI camera starts and stops with each session";
    case SettingItem::Engine: return "A Bluetooth OBD-II adapter";
    case SettingItem::Camera: return "A DJI Osmo Action camera: Track Rat as its GPS remote";
    default: return "";
  }
}

// The Session length setting in minutes, 0 = off.
inline int sessionMinutes(const SettingsData& d) { return d.sessionLength >= 1 && d.sessionLength <= 3 ? 15 + 5 * d.sessionLength : 0; }

inline void valueText(const SettingsData& d, SettingItem item, char* buf, size_t n) {
  switch (item) {
    case SettingItem::Units: snprintf(buf, n, "%s", d.units == SpeedUnits::Mph ? "mph" : "km/h"); break;
    case SettingItem::Upside: snprintf(buf, n, "%s", upsideDown(d) ? "Upside down" : "Normal"); break;
    case SettingItem::Brightness: snprintf(buf, n, "%d%%", d.brightness); break;
    case SettingItem::Compare:
      snprintf(buf, n, "%s", d.compareTo == CompareTo::Day ? "Best of the day" : d.compareTo == CompareTo::Ever ? "Best ever" : "This session");
      break;
    case SettingItem::Big:
      snprintf(buf, n, "%s", d.bigNumber == BigNumber::Delta ? "Delta" : d.bigNumber == BigNumber::Predicted ? "Predicted lap" : "Lap time");
      break;
    case SettingItem::Colours: snprintf(buf, n, "%s", d.deltaColours == DeltaColours::BlueOrange ? "Blue / orange" : "Green / red"); break;
    case SettingItem::SessionLen:
      if (sessionMinutes(d) == 0) snprintf(buf, n, "Off");
      else snprintf(buf, n, "%d min", sessionMinutes(d));
      break;
    case SettingItem::Gps: snprintf(buf, n, "%s", d.gpsMode == GpsMode::Fast20 ? "20 Hz, GPS + Galileo" : "10 Hz, all systems"); break;
    case SettingItem::Bluetooth: snprintf(buf, n, "%s", d.bluetooth ? "On" : "Off"); break;
    case SettingItem::TimeZone: {
      int m = d.utcOffsetMin < 0 ? -d.utcOffsetMin : d.utcOffsetMin;
      snprintf(buf, n, "UTC%s%d:%02d", d.utcOffsetMin < 0 ? "−" : "+", m / 60, m % 60);
      break;
    }
    case SettingItem::RoadDrives: snprintf(buf, n, "%s", d.recordRoadDrives ? "On" : "Off"); break;
    case SettingItem::MinLap:
      if (d.minLap == 0 || d.minLap >= MIN_LAP_CHOICES) snprintf(buf, n, "Auto");
      else snprintf(buf, n, "%d s", MIN_LAP_S[d.minLap]);
      break;
    case SettingItem::Kart: snprintf(buf, n, "%s", d.kartEverywhere ? "On" : "Off"); break;
    case SettingItem::CameraRec: snprintf(buf, n, "%s", d.cameraWithSessions ? "With sessions" : "Manual"); break;
    default: if (n) buf[0] = 0;
  }
}

// --- the Wi-Fi page: GET /api/settings sends toJson(), POST sets fields by name ---

// {"units":"mph","upsideDown":false,...}. Returns the length, or 0 if buf is too small.
inline size_t toJson(const SettingsData& d, char* buf, size_t n) {
  static const char* const COMPARE[] = {"day", "ever", "session"};
  static const char* const BIG[] = {"delta", "predicted", "lap"};
  static const char* const SESSION_TEXT[] = {"off", "20", "25", "30"};
  static const char* const MIN_LAP_TEXT[] = {"auto", "10", "15", "30"};  // MIN_LAP_S
  int len = snprintf(buf, n,
                     "{\"units\":\"%s\",\"upsideDown\":%s,\"brightness\":%d,\"compareTo\":\"%s\",\"bigNumber\":\"%s\","
                     "\"deltaColours\":\"%s\",\"sessionMin\":\"%s\",\"gpsMode\":\"%s\",\"bluetooth\":%s,\"tzMin\":%d,\"recordRoadDrives\":%s,"
                     "\"minLap\":\"%s\",\"kartEverywhere\":%s,\"cameraRecording\":\"%s\",\"bluetoothBuilt\":%s}",
                     d.units == SpeedUnits::Mph ? "mph" : "kmh", upsideDown(d) ? "true" : "false", d.brightness,
                     COMPARE[(int)d.compareTo % 3], BIG[(int)d.bigNumber % 3],
                     d.deltaColours == DeltaColours::BlueOrange ? "blueorange" : "greenred", SESSION_TEXT[sessionMinutes(d) ? d.sessionLength : 0], d.gpsMode == GpsMode::Fast20 ? "20hz" : "10hz",
                     d.bluetooth ? "true" : "false", d.utcOffsetMin, d.recordRoadDrives ? "true" : "false",
                     MIN_LAP_TEXT[d.minLap < MIN_LAP_CHOICES ? d.minLap : 0], d.kartEverywhere ? "true" : "false",
                     d.cameraWithSessions ? "sessions" : "manual", BLE_ENABLED ? "true" : "false");
  return len > 0 && (size_t)len < n ? (size_t)len : 0;
}

// "true"/"1"/"on" or "false"/"0"/"off"
inline bool parseBool(const char* v, bool* out) {
  if (!strcmp(v, "true") || !strcmp(v, "1") || !strcmp(v, "on")) return *out = true, true;
  if (!strcmp(v, "false") || !strcmp(v, "0") || !strcmp(v, "off")) return *out = false, true;
  return false;
}

// A whole number between lo and hi, nothing else ("+5" and "-30" are fine).
inline bool parseInt(const char* v, int lo, int hi, int* out) {
  if (!v || !*v) return false;
  char* end = nullptr;
  long x = strtol(v, &end, 10);
  if (*end || x < lo || x > hi) return false;
  *out = (int)x;
  return true;
}

// Sets one setting from the page's name and text value. False (and d
// unchanged) for an unknown name or a value that isn't allowed.
inline bool setField(SettingsData& d, const char* key, const char* v) {
  if (!key || !v) return false;
  bool b;
  int x;
  if (!strcmp(key, "units")) {
    if (!strcmp(v, "kmh")) d.units = SpeedUnits::Kmh;
    else if (!strcmp(v, "mph")) d.units = SpeedUnits::Mph;
    else return false;
  } else if (!strcmp(key, "upsideDown")) {
    if (!parseBool(v, &b)) return false;
    const uint8_t up = DISPLAY_ROTATION == 3 ? 3 : 1;
    d.rotation = b ? (up == 1 ? 3 : 1) : up;
  } else if (!strcmp(key, "brightness")) {
    if (!parseInt(v, BRIGHTNESS_MIN, BRIGHTNESS_MAX, &x)) return false;
    d.brightness = (uint8_t)x;
  } else if (!strcmp(key, "compareTo")) {
    if (!strcmp(v, "day")) d.compareTo = CompareTo::Day;
    else if (!strcmp(v, "ever")) d.compareTo = CompareTo::Ever;
    else if (!strcmp(v, "session")) d.compareTo = CompareTo::Session;
    else return false;
  } else if (!strcmp(key, "bigNumber")) {
    if (!strcmp(v, "delta")) d.bigNumber = BigNumber::Delta;
    else if (!strcmp(v, "predicted")) d.bigNumber = BigNumber::Predicted;
    else if (!strcmp(v, "lap")) d.bigNumber = BigNumber::LapTime;
    else return false;
  } else if (!strcmp(key, "deltaColours")) {
    if (!strcmp(v, "greenred")) d.deltaColours = DeltaColours::GreenRed;
    else if (!strcmp(v, "blueorange")) d.deltaColours = DeltaColours::BlueOrange;
    else return false;
  } else if (!strcmp(key, "sessionMin")) {  // "off", "20", "25" or "30"
    if (!strcmp(v, "off")) d.sessionLength = 0;
    else if (!strcmp(v, "20")) d.sessionLength = 1;
    else if (!strcmp(v, "25")) d.sessionLength = 2;
    else if (!strcmp(v, "30")) d.sessionLength = 3;
    else return false;
  } else if (!strcmp(key, "gpsMode")) {
    if (!strcmp(v, "20hz")) d.gpsMode = GpsMode::Fast20;
    else if (!strcmp(v, "10hz")) d.gpsMode = GpsMode::AllSystems10;
    else return false;
  } else if (!strcmp(key, "bluetooth")) {
    if (!parseBool(v, &b)) return false;
    d.bluetooth = b;
  } else if (!strcmp(key, "tzMin")) {
    if (!parseInt(v, UTC_MIN, UTC_MAX, &x)) return false;
    d.utcOffsetMin = (int16_t)x;
    sanitize(d);  // whole quarter hours
  } else if (!strcmp(key, "recordRoadDrives")) {
    if (!parseBool(v, &b)) return false;
    d.recordRoadDrives = b;
  } else if (!strcmp(key, "minLap")) {  // "auto", or one of MIN_LAP_S in seconds
    if (!strcmp(v, "auto")) d.minLap = 0;
    else if (!strcmp(v, "10")) d.minLap = 1;
    else if (!strcmp(v, "15")) d.minLap = 2;
    else if (!strcmp(v, "30")) d.minLap = 3;
    else return false;
  } else if (!strcmp(key, "kartEverywhere")) {
    if (!parseBool(v, &b)) return false;
    d.kartEverywhere = b;
  } else if (!strcmp(key, "cameraRecording")) {  // "sessions" or "manual"
    if (!strcmp(v, "sessions")) d.cameraWithSessions = true;
    else if (!strcmp(v, "manual")) d.cameraWithSessions = false;
    else return false;
  } else {
    return false;
  }
  return true;
}

// --- things other code asks of a setting ---

// Karting (a track with "kind": "kart", or every track with Kart everywhere on):
// smaller lines by default, and a shorter minimum lap.
inline bool kart(const SettingsData& d, bool trackIsKart) { return trackIsKart || d.kartEverywhere; }
inline float defaultGateM(bool kart) { return kart ? (float)KART_GATE_HALF_WIDTH_M : (float)DEFAULT_GATE_HALF_WIDTH_M; }
inline uint32_t minLapMs(const SettingsData& d, bool kart) {
  if (d.minLap > 0 && d.minLap < MIN_LAP_CHOICES) return MIN_LAP_S[d.minLap] * 1000UL;
  return kart ? KART_MIN_LAP_MS : MIN_LAP_TIME_MS;
}

inline float speedValue(float kmh, SpeedUnits u) { return u == SpeedUnits::Mph ? kmh / KMH_PER_MPH : kmh; }
inline const char* speedUnitLabel(SpeedUnits u) { return u == SpeedUnits::Mph ? "mph" : "km/h"; }

// u-blox update rate and satellite systems for a GPS mode. More systems means
// more satellites but a lower top rate (M10: 4 systems 10 Hz, 2 systems 20 Hz).
struct GpsSystems {
  int rateHz;
  bool galileo, glonass, beidou;  // GPS (with QZSS) is always on
};
inline GpsSystems gpsSystems(GpsMode m) {
  if (m == GpsMode::AllSystems10) return {10, true, true, true};
  return {GPS_TARGET_RATE_HZ >= 20 ? GPS_TARGET_RATE_HZ : 20, GPS_USE_GALILEO != 0, GPS_USE_GLONASS != 0, GPS_USE_BEIDOU != 0};
}

// What the timing screen really gives the largest digits. Delta and predicted
// lap need a live delta against a best lap; without one it's the lap time.
inline BigNumber bigNumberShown(BigNumber wanted, bool haveDelta) { return haveDelta ? wanted : BigNumber::LapTime; }

// Predicted lap time: the best lap plus the live delta. Never negative.
inline uint32_t predictedLapMs(uint32_t bestMs, int32_t deltaMs) {
  int64_t t = (int64_t)bestMs + deltaMs;
  return t < 0 ? 0 : (uint32_t)t;
}

// Backlight PWM duty for a brightness percentage. Squared, because the eye
// is roughly logarithmic: 50% should look about half as bright, not 70%.
inline uint32_t backlightDuty(uint8_t percent, uint32_t maxDuty) {
  if (percent > 100) percent = 100;
  uint32_t duty = (uint32_t)((uint64_t)maxDuty * percent * percent / 10000);
  return duty < 1 ? 1 : duty;  // never fully off: that's what the backlight switch is for
}

// Time of day (ms since local midnight) from UTC milliseconds since midnight.
inline uint32_t localTimeOfDayMs(uint32_t utcMs, int16_t offsetMin) {
  const int64_t day = 86400000LL;
  int64_t t = ((int64_t)utcMs + (int64_t)offsetMin * 60000) % day;
  if (t < 0) t += day;
  return (uint32_t)t;
}

}  // namespace settings

#endif  // SETTINGS_DATA_H
