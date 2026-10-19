/**
 * Host-side tests for the settings rules (include/SettingsData.h) and the
 * input flow of the settings screen and first-boot setup (include/SettingsFlow.h).
 * The last group drives a first boot and a two-lap session through them.
 *
 *   g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_settings.cpp src/LapTimer.cpp -o /tmp/st && /tmp/st
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "LapTimer.h"
#include "SettingsFlow.h"
#include "check.h"

using namespace settings;

static int checks = 0;
#define EXPECT(cond, ...)    \
  do {                       \
    checks++;                \
    CHECK(cond, __VA_ARGS__); \
  } while (0)

#define MINUS "\xE2\x88\x92"  // the minus sign valueText uses for the time zone

static SettingItem itemAt(int i) { return (SettingItem)i; }

static std::string text(const SettingsData& d, SettingItem item) {
  char buf[48];
  valueText(d, item, buf, sizeof(buf));
  return buf;
}

static bool valid(const SettingsData& d) {
  SettingsData c = d;
  sanitize(c);
  return c == d;
}

static SettingsData randomSettings() {
  SettingsData d = defaults();
  d.setupDone = testRand() & 1;
  d.units = (SpeedUnits)(testRand() % 2);
  d.rotation = (testRand() & 1) ? 3 : 1;
  d.brightness = (uint8_t)(10 * (1 + testRand() % 10));
  d.compareTo = (CompareTo)(testRand() % 3);
  d.bigNumber = (BigNumber)(testRand() % 3);
  d.gpsMode = (GpsMode)(testRand() % 2);
  d.bluetooth = testRand() & 1;
  d.utcOffsetMin = (int16_t)(15 * (testRand() % 105 - 48));  // -720..840 in quarter hours
  d.recordRoadDrives = testRand() & 1;
  return d;
}

// Applies a flat {"key":"value","key":5,...} the way the Wi-Fi page posts it, one setField per pair.
static int applyJson(SettingsData& d, const char* j, int* rejected) {
  int ok = 0;
  *rejected = 0;
  const char* p = j;
  while ((p = strchr(p, '"')) != nullptr) {
    const char* ke = strchr(p + 1, '"');
    std::string key(p + 1, ke);
    p = ke + 2;  // past the closing quote and the colon
    std::string val;
    if (*p == '"') {
      const char* ve = strchr(p + 1, '"');
      val.assign(p + 1, ve);
      p = ve + 1;
    } else {
      const char* ve = p;
      while (*ve && *ve != ',' && *ve != '}') ve++;
      val.assign(p, ve);
      p = ve;
    }
    if (setField(d, key.c_str(), val.c_str())) ok++;
    else (*rejected)++;
  }
  return ok;
}

// --- 1. defaults ---------------------------------------------------------------

static void testDefaults() {
  const SettingsData d = defaults();
  EXPECT(!d.setupDone, "first boot must ask the setup questions");
  EXPECT(d.units == (DEFAULT_SPEED_MPH ? SpeedUnits::Mph : SpeedUnits::Kmh), "units");
  EXPECT(d.rotation == DISPLAY_ROTATION, "rotation %d vs DISPLAY_ROTATION %d", d.rotation, DISPLAY_ROTATION);
  EXPECT(!upsideDown(d), "defaults are right way up");
  EXPECT(d.brightness == DEFAULT_BRIGHTNESS_PCT, "brightness %d", d.brightness);
  EXPECT(d.compareTo == (CompareTo)DEFAULT_COMPARE_TO, "compareTo");
  EXPECT(d.bigNumber == (BigNumber)DEFAULT_BIG_NUMBER, "bigNumber");
  EXPECT(d.gpsMode == (GPS_TARGET_RATE_HZ >= 20 ? GpsMode::Fast20 : GpsMode::AllSystems10), "gpsMode");
  EXPECT(d.bluetooth == (BLE_ENABLED != 0), "bluetooth");
  EXPECT(d.utcOffsetMin == DEFAULT_UTC_OFFSET_MIN, "utc offset %d", d.utcOffsetMin);
  EXPECT(d.recordRoadDrives == (DEFAULT_RECORD_ROAD_DRIVES != 0), "recordRoadDrives");

  SettingsData s = d;
  sanitize(s);
  EXPECT(s == d, "sanitize changed the defaults");
  sanitize(s);
  EXPECT(s == d && !(s != d), "sanitize is not idempotent");

  SettingsData e = d;
  e.brightness = 50;
  EXPECT(e != d && !(e == d), "operator== must see a different brightness");
  for (int i = 0; i < SETTING_ITEM_COUNT; i++) {
    SettingsData f = d;
    adjust(f, itemAt(i), +1, true);
    EXPECT(f != d, "operator== missed a change to item %d", i);
  }
  printf("  defaults match Config.h and survive sanitize\n");
}

// --- 2. corrupt values from flash ---------------------------------------------

static void testSanitize() {
  const SettingsData def = defaults();
  SettingsData d = def;

  d.units = (SpeedUnits)2;
  sanitize(d);
  EXPECT(d.units == def.units, "units 2");
  d.units = (SpeedUnits)255;
  sanitize(d);
  EXPECT(d.units == def.units, "units 255");

  for (int r : {0, 2, 4, 255}) {
    d = def;
    d.rotation = (uint8_t)r;
    sanitize(d);
    EXPECT(d.rotation == def.rotation, "rotation %d became %d", r, d.rotation);
  }
  for (int r : {1, 3}) {
    d = def;
    d.rotation = (uint8_t)r;
    sanitize(d);
    EXPECT(d.rotation == r, "rotation %d must stay", r);
  }

  struct B { int in, want; };
  for (B b : {B{0, 10}, B{1, 10}, B{9, 10}, B{10, 10}, B{55, 55}, B{100, 100}, B{101, 100}, B{250, 100}, B{255, 100}}) {
    d = def;
    d.brightness = (uint8_t)b.in;
    sanitize(d);
    EXPECT(d.brightness == b.want, "brightness %d became %d, want %d", b.in, d.brightness, b.want);
  }

  for (int v : {3, 4, 255}) {
    d = def;
    d.compareTo = (CompareTo)v;
    d.bigNumber = (BigNumber)v;
    d.gpsMode = (GpsMode)v;
    sanitize(d);
    EXPECT(d.compareTo == def.compareTo && d.bigNumber == def.bigNumber && d.gpsMode == def.gpsMode,
           "enums %d: %d %d %d", v, (int)d.compareTo, (int)d.bigNumber, (int)d.gpsMode);
  }
  for (int v = 0; v <= 2; v++) {  // good values stay
    d = def;
    d.compareTo = (CompareTo)v;
    d.bigNumber = (BigNumber)v;
    sanitize(d);
    EXPECT((int)d.compareTo == v && (int)d.bigNumber == v, "good enum %d changed", v);
  }

  struct T { int in, want; };
  for (T t : {T{-9999, -720}, T{5000, 840}, T{-721, -720}, T{841, 840}, T{INT16_MIN, -720}, T{INT16_MAX, 840}, T{0, 0},
              T{345, 345}, T{-570, -570}, T{840, 840}, T{-720, -720}, T{7, 0}, T{8, 15}, T{-7, 0}, T{-8, -15},
              T{22, 15}, T{23, 30}, T{-22, -15}, T{-23, -30}, T{839, 840}, T{-719, -720}, T{331, 330}, T{-331, -330}}) {
    d = def;
    d.utcOffsetMin = (int16_t)t.in;
    sanitize(d);
    EXPECT(d.utcOffsetMin == t.want, "tz %d became %d, want %d", t.in, d.utcOffsetMin, t.want);
  }
  // every minute value: in range, a whole quarter hour, the nearest one, and the same either side of zero
  bool shape = true, mirror = true;
  for (int m = UTC_MIN; m <= UTC_MAX; m++) {
    SettingsData a = def, b = def;
    a.utcOffsetMin = (int16_t)m;
    sanitize(a);
    shape = shape && a.utcOffsetMin % 15 == 0 && abs(a.utcOffsetMin - m) <= 7 && a.utcOffsetMin >= UTC_MIN &&
            a.utcOffsetMin <= UTC_MAX;
    if (m >= -UTC_MAX && m <= -UTC_MIN) {
      b.utcOffsetMin = (int16_t)-m;
      sanitize(b);
      mirror = mirror && b.utcOffsetMin == -a.utcOffsetMin;
    }
  }
  EXPECT(shape, "every minute 720 west..840 east must land on the nearest valid quarter hour");
  EXPECT(mirror, "rounding must be the same on both sides of zero");

  // everything wrong at once
  d = def;
  d.units = (SpeedUnits)9;
  d.rotation = 2;
  d.brightness = 0;
  d.compareTo = (CompareTo)9;
  d.bigNumber = (BigNumber)9;
  d.gpsMode = (GpsMode)9;
  d.utcOffsetMin = -9999;
  d.bluetooth = !def.bluetooth;
  d.recordRoadDrives = !def.recordRoadDrives;
  d.setupDone = true;
  sanitize(d);
  EXPECT(valid(d), "result must be valid");
  EXPECT(d.brightness == 10 && d.utcOffsetMin == -720 && d.rotation == def.rotation && d.units == def.units,
         "all-bad: brightness %d tz %d", d.brightness, d.utcOffsetMin);
  EXPECT(d.setupDone && d.bluetooth == !def.bluetooth && d.recordRoadDrives == !def.recordRoadDrives,
         "sanitize must keep the flags that cannot be out of range");
  printf("  corrupt flash values put right, time zone rounded to quarter hours\n");
}

// --- 3. adjust -----------------------------------------------------------------

static void testAdjust() {
  const SettingsData base = defaults();

  // two-value items flip either way, with or without wrap
  for (SettingItem it : {SettingItem::Units, SettingItem::Upside, SettingItem::Gps, SettingItem::Bluetooth, SettingItem::RoadDrives}) {
    for (int dir : {-1, 1}) {
      for (bool wrap : {false, true}) {
        SettingsData d = base;
        adjust(d, it, dir, wrap);
        EXPECT(d != base && text(d, it) != text(base, it), "item %d dir %d wrap %d did not flip", (int)it, dir, wrap);
        adjust(d, it, dir, wrap);
        EXPECT(d == base, "item %d flipped twice must be back", (int)it);
      }
    }
  }
  {
    SettingsData d = base;
    d.rotation = 3;
    adjust(d, SettingItem::Upside, 1, false);
    EXPECT(d.rotation == 1, "rotation 3 flips to 1");
    adjust(d, SettingItem::Upside, -1, false);
    EXPECT(d.rotation == 3, "rotation 1 flips to 3");
  }

  // three-value items
  for (int v = 0; v < 3; v++) {
    for (int which = 0; which < 2; which++) {
      SettingItem it = which ? SettingItem::Big : SettingItem::Compare;
      auto get = [&](const SettingsData& d) { return which ? (int)d.bigNumber : (int)d.compareTo; };
      auto set = [&](SettingsData& d, int x) {
        if (which) d.bigNumber = (BigNumber)x;
        else d.compareTo = (CompareTo)x;
      };
      SettingsData d = base;
      set(d, v);
      adjust(d, it, +1, false);
      EXPECT(get(d) == (v == 2 ? 2 : v + 1), "item %d: %d + clamp -> %d", (int)it, v, get(d));
      set(d, v);
      adjust(d, it, -1, false);
      EXPECT(get(d) == (v == 0 ? 0 : v - 1), "item %d: %d - clamp -> %d", (int)it, v, get(d));
      set(d, v);
      adjust(d, it, +1, true);
      EXPECT(get(d) == (v + 1) % 3, "item %d: %d + wrap -> %d", (int)it, v, get(d));
      set(d, v);
      adjust(d, it, -1, true);
      EXPECT(get(d) == (v + 2) % 3, "item %d: %d - wrap -> %d", (int)it, v, get(d));
    }
  }

  // brightness: steps of 10, stops or goes round at both ends
  {
    SettingsData d = base;
    d.brightness = 50;
    adjust(d, SettingItem::Brightness, +1, false);
    EXPECT(d.brightness == 60, "50 + -> %d", d.brightness);
    adjust(d, SettingItem::Brightness, -1, false);
    adjust(d, SettingItem::Brightness, -1, false);
    EXPECT(d.brightness == 40, "50 - - -> %d", d.brightness);
    d.brightness = 100;
    adjust(d, SettingItem::Brightness, +1, false);
    EXPECT(d.brightness == 100, "100 + clamps, got %d", d.brightness);
    adjust(d, SettingItem::Brightness, +1, true);
    EXPECT(d.brightness == 10, "100 + wraps to 10, got %d", d.brightness);
    adjust(d, SettingItem::Brightness, -1, false);
    EXPECT(d.brightness == 10, "10 - clamps, got %d", d.brightness);
    adjust(d, SettingItem::Brightness, -1, true);
    EXPECT(d.brightness == 100, "10 - wraps to 100, got %d", d.brightness);
    d.brightness = 95;  // off the 10 grid: never above 100
    adjust(d, SettingItem::Brightness, +1, false);
    EXPECT(d.brightness == 100, "95 + clamps to 100, got %d", d.brightness);
    // ten wrapping steps visit every value once and come back
    d.brightness = 10;
    bool seen[11] = {false};
    for (int i = 0; i < 10; i++) {
      seen[d.brightness / 10] = true;
      adjust(d, SettingItem::Brightness, +1, true);
    }
    bool all = true;
    for (int i = 1; i <= 10; i++) all = all && seen[i];
    EXPECT(all && d.brightness == 10, "wrapping round the brightness");
  }

  // time zone: steps of 30 minutes
  {
    SettingsData d = base;
    d.utcOffsetMin = 0;
    adjust(d, SettingItem::TimeZone, +1, false);
    EXPECT(d.utcOffsetMin == 30, "0 + -> %d", d.utcOffsetMin);
    adjust(d, SettingItem::TimeZone, -1, false);
    adjust(d, SettingItem::TimeZone, -1, false);
    EXPECT(d.utcOffsetMin == -30, "0 - -> %d", d.utcOffsetMin);
    d.utcOffsetMin = 840;
    adjust(d, SettingItem::TimeZone, +1, false);
    EXPECT(d.utcOffsetMin == 840, "840 + clamps, got %d", d.utcOffsetMin);
    adjust(d, SettingItem::TimeZone, +1, true);
    EXPECT(d.utcOffsetMin == -720, "840 + wraps to -720, got %d", d.utcOffsetMin);
    adjust(d, SettingItem::TimeZone, -1, false);
    EXPECT(d.utcOffsetMin == -720, "-720 - clamps, got %d", d.utcOffsetMin);
    adjust(d, SettingItem::TimeZone, -1, true);
    EXPECT(d.utcOffsetMin == 840, "-720 - wraps to 840, got %d", d.utcOffsetMin);
    d.utcOffsetMin = 825;  // a quarter-hour zone near the top
    adjust(d, SettingItem::TimeZone, +1, false);
    EXPECT(d.utcOffsetMin == 840, "825 + clamps to 840, got %d", d.utcOffsetMin);
    d.utcOffsetMin = 345;  // Nepal
    adjust(d, SettingItem::TimeZone, +1, false);
    EXPECT(d.utcOffsetMin == 375 && valid(d), "345 + -> %d", d.utcOffsetMin);
    d.utcOffsetMin = 0;
    for (int i = 0; i < 53; i++) adjust(d, SettingItem::TimeZone, +1, true);  // -720..840 is 53 values
    EXPECT(d.utcOffsetMin == 0, "53 wrapping steps are a full turn, got %d", d.utcOffsetMin);
  }

  // the end marker is not an item
  {
    SettingsData d = base;
    adjust(d, SettingItem::Count, +1, true);
    EXPECT(d == base, "SettingItem::Count changed something");
  }

  // 200 steps of every item, every way, from the defaults and from random settings
  testSeed(5);
  for (int start = 0; start < 4; start++) {
    for (int i = 0; i < SETTING_ITEM_COUNT; i++) {
      for (int dir : {-1, 1}) {
        for (bool wrap : {false, true}) {
          SettingsData d = start == 0 ? base : randomSettings();
          bool stayedValid = true, textOk = true, othersKept = true;
          for (int n = 0; n < 200; n++) {
            SettingsData before = d;
            adjust(d, itemAt(i), dir, wrap);
            stayedValid = stayedValid && valid(d);
            for (int j = 0; j < SETTING_ITEM_COUNT; j++) {
              textOk = textOk && !text(d, itemAt(j)).empty();
              if (j != i) othersKept = othersKept && text(d, itemAt(j)) == text(before, itemAt(j));
            }
            othersKept = othersKept && d.setupDone == before.setupDone;
          }
          EXPECT(stayedValid && textOk && othersKept, "200 steps of item %d dir %d wrap %d: valid %d text %d others %d", i, dir,
                 wrap, stayedValid, textOk, othersKept);
        }
      }
    }
  }
  printf("  adjust: flips, clamps, wraps and 200 steps of every item\n");
}

// --- 4. text -------------------------------------------------------------------

static void testValueText() {
  SettingsData d = defaults();
  d.units = SpeedUnits::Kmh;
  EXPECT(text(d, SettingItem::Units) == "km/h", "units text");
  d.units = SpeedUnits::Mph;
  EXPECT(text(d, SettingItem::Units) == "mph", "units text");
  d.rotation = DISPLAY_ROTATION;
  EXPECT(text(d, SettingItem::Upside) == "Normal", "screen text '%s'", text(d, SettingItem::Upside).c_str());
  d.rotation = DISPLAY_ROTATION == 1 ? 3 : 1;
  EXPECT(text(d, SettingItem::Upside) == "Upside down", "screen text");
  d.brightness = 100;
  EXPECT(text(d, SettingItem::Brightness) == "100%", "brightness text");
  d.brightness = 10;
  EXPECT(text(d, SettingItem::Brightness) == "10%", "brightness text");
  d.compareTo = CompareTo::Day;
  EXPECT(text(d, SettingItem::Compare) == "Best of the day", "compare text");
  d.compareTo = CompareTo::Ever;
  EXPECT(text(d, SettingItem::Compare) == "Best ever", "compare text");
  d.compareTo = CompareTo::Session;
  EXPECT(text(d, SettingItem::Compare) == "This session", "compare text");
  d.bigNumber = BigNumber::Delta;
  EXPECT(text(d, SettingItem::Big) == "Delta", "big text");
  d.bigNumber = BigNumber::Predicted;
  EXPECT(text(d, SettingItem::Big) == "Predicted lap", "big text");
  d.bigNumber = BigNumber::LapTime;
  EXPECT(text(d, SettingItem::Big) == "Lap time", "big text");
  d.gpsMode = GpsMode::Fast20;
  EXPECT(text(d, SettingItem::Gps) == "20 Hz, GPS + Galileo", "gps text");
  d.gpsMode = GpsMode::AllSystems10;
  EXPECT(text(d, SettingItem::Gps) == "10 Hz, all systems", "gps text");
  d.bluetooth = true;
  d.recordRoadDrives = false;
  EXPECT(text(d, SettingItem::Bluetooth) == "On" && text(d, SettingItem::RoadDrives) == "Off", "on/off text");
  d.bluetooth = false;
  d.recordRoadDrives = true;
  EXPECT(text(d, SettingItem::Bluetooth) == "Off" && text(d, SettingItem::RoadDrives) == "On", "on/off text");

  struct Z { int min; const char* want; };
  for (Z z : {Z{-300, "UTC" MINUS "5:00"}, Z{330, "UTC+5:30"}, Z{0, "UTC+0:00"}, Z{-30, "UTC" MINUS "0:30"},
              Z{840, "UTC+14:00"}, Z{-720, "UTC" MINUS "12:00"}, Z{345, "UTC+5:45"}, Z{-570, "UTC" MINUS "9:30"},
              Z{60, "UTC+1:00"}, Z{-15, "UTC" MINUS "0:15"}}) {
    d.utcOffsetMin = (int16_t)z.min;
    EXPECT(text(d, SettingItem::TimeZone) == z.want, "tz %d: '%s', want '%s'", z.min, text(d, SettingItem::TimeZone).c_str(), z.want);
  }

  // not an item: empty. Too small a buffer: cut short, still ends in 0, nothing written past n
  EXPECT(text(d, SettingItem::Count).empty(), "Count has no text");
  {
    char buf[16];
    memset(buf, 'x', sizeof(buf));
    d.utcOffsetMin = -300;
    valueText(d, SettingItem::TimeZone, buf, 4);
    EXPECT(buf[3] == 0 && strlen(buf) == 3 && buf[4] == 'x', "small buffer: '%s'", buf);
    memset(buf, 'x', sizeof(buf));
    valueText(d, SettingItem::Count, buf, 4);
    EXPECT(buf[0] == 0 && buf[1] == 'x', "Count in a small buffer");
    memset(buf, 'x', sizeof(buf));
    valueText(d, SettingItem::Units, buf, 0);
    EXPECT(buf[0] == 'x', "n = 0 must write nothing");
  }

  // names and notes
  for (int i = 0; i < SETTING_ITEM_COUNT; i++) {
    EXPECT(itemName(itemAt(i))[0] != 0, "item %d has no name", i);
    EXPECT(itemNote(itemAt(i)) != nullptr, "item %d note is null", i);
    for (int j = 0; j < i; j++) EXPECT(strcmp(itemName(itemAt(i)), itemName(itemAt(j))) != 0, "items %d and %d share a name", i, j);
  }
  EXPECT(itemName(SettingItem::Count)[0] == 0, "Count has no name");
  printf("  value text, including the UTF-8 minus in the time zone\n");
}

// --- 5. the Wi-Fi page: toJson and setField ----------------------------------------

static void testJson() {
  SettingsData d = defaults();
  d.setupDone = true;
  d.units = SpeedUnits::Mph;
  d.rotation = DISPLAY_ROTATION == 1 ? 3 : 1;
  d.brightness = 70;
  d.compareTo = CompareTo::Ever;
  d.bigNumber = BigNumber::Predicted;
  d.gpsMode = GpsMode::AllSystems10;
  d.bluetooth = false;
  d.utcOffsetMin = -330;
  d.recordRoadDrives = false;

  char buf[400];
  size_t len = toJson(d, buf, sizeof(buf));
  std::string want = std::string("{\"units\":\"mph\",\"upsideDown\":true,\"brightness\":70,\"compareTo\":\"ever\",") +
                     "\"bigNumber\":\"predicted\",\"gpsMode\":\"10hz\",\"bluetooth\":false,\"tzMin\":-330," +
                     "\"recordRoadDrives\":false,\"bluetoothBuilt\":" + (BLE_ENABLED ? "true" : "false") + "}";
  EXPECT(len == strlen(buf) && len > 0, "length %zu vs strlen %zu", len, strlen(buf));
  EXPECT(want == buf, "json:\n  got  %s\n  want %s", buf, want.c_str());

  // well-formed for any settings
  testSeed(9);
  bool formed = true;
  for (int i = 0; i < 200; i++) {
    SettingsData r = randomSettings();
    size_t n = toJson(r, buf, sizeof(buf));
    std::string j(buf);
    bool ok = n == j.size() && j.front() == '{' && j.back() == '}' && std::count(j.begin(), j.end(), '"') % 2 == 0 &&
              std::count(j.begin(), j.end(), ',') == 9;
    for (const char* key : {"units", "upsideDown", "brightness", "compareTo", "bigNumber", "gpsMode", "bluetooth", "tzMin",
                            "recordRoadDrives", "bluetoothBuilt"})
      ok = ok && j.find(std::string("\"") + key + "\":") != std::string::npos;
    formed = formed && ok;
  }
  EXPECT(formed, "toJson must give one well-formed object with every key");

  // a buffer that is too small gives 0, never a cut-off object
  EXPECT(toJson(d, buf, len + 1) == len, "a buffer of exactly length + 1 fits");
  EXPECT(toJson(d, buf, len) == 0, "no room for the 0 byte");
  EXPECT(toJson(d, buf, len - 1) == 0, "one byte short");
  EXPECT(toJson(d, buf, 10) == 0 && toJson(d, buf, 1) == 0, "tiny buffers");
  EXPECT(toJson(d, nullptr, 0) == 0, "no buffer");
}

static void testSetField() {
  SettingsData d = randomSettings();
  d.setupDone = true;

  // every valid name and value
  EXPECT(setField(d, "units", "kmh") && d.units == SpeedUnits::Kmh, "units kmh");
  EXPECT(setField(d, "units", "mph") && d.units == SpeedUnits::Mph, "units mph");
  const uint8_t up = DISPLAY_ROTATION == 3 ? 3 : 1, down = up == 1 ? 3 : 1;
  for (const char* v : {"true", "1", "on"}) {
    d.rotation = up;
    EXPECT(setField(d, "upsideDown", v) && d.rotation == down && upsideDown(d), "upsideDown %s", v);
  }
  for (const char* v : {"false", "0", "off"}) {
    d.rotation = down;
    EXPECT(setField(d, "upsideDown", v) && d.rotation == DISPLAY_ROTATION && !upsideDown(d), "upsideDown %s", v);
  }
  struct N { const char* in; int want; };
  for (N n : {N{"10", 10}, N{"55", 55}, N{"100", 100}, N{"+50", 50}}) {
    EXPECT(setField(d, "brightness", n.in) && d.brightness == n.want, "brightness %s", n.in);
  }
  EXPECT(setField(d, "compareTo", "day") && d.compareTo == CompareTo::Day, "compareTo day");
  EXPECT(setField(d, "compareTo", "ever") && d.compareTo == CompareTo::Ever, "compareTo ever");
  EXPECT(setField(d, "compareTo", "session") && d.compareTo == CompareTo::Session, "compareTo session");
  EXPECT(setField(d, "bigNumber", "delta") && d.bigNumber == BigNumber::Delta, "bigNumber delta");
  EXPECT(setField(d, "bigNumber", "predicted") && d.bigNumber == BigNumber::Predicted, "bigNumber predicted");
  EXPECT(setField(d, "bigNumber", "lap") && d.bigNumber == BigNumber::LapTime, "bigNumber lap");
  EXPECT(setField(d, "gpsMode", "20hz") && d.gpsMode == GpsMode::Fast20, "gpsMode 20hz");
  EXPECT(setField(d, "gpsMode", "10hz") && d.gpsMode == GpsMode::AllSystems10, "gpsMode 10hz");
  for (const char* v : {"true", "1", "on"}) EXPECT(setField(d, "bluetooth", v) && d.bluetooth, "bluetooth %s", v);
  for (const char* v : {"false", "0", "off"}) EXPECT(setField(d, "bluetooth", v) && !d.bluetooth, "bluetooth %s", v);
  for (const char* v : {"true", "1", "on"}) EXPECT(setField(d, "recordRoadDrives", v) && d.recordRoadDrives, "road %s", v);
  for (const char* v : {"false", "0", "off"}) EXPECT(setField(d, "recordRoadDrives", v) && !d.recordRoadDrives, "road %s", v);
  for (N n : {N{"-720", -720}, N{"840", 840}, N{"0", 0}, N{"-300", -300}, N{"+330", 330}, N{"345", 345}, N{"331", 330},
              N{"-331", -330}, N{"7", 0}, N{"8", 15}}) {
    EXPECT(setField(d, "tzMin", n.in) && d.utcOffsetMin == n.want, "tzMin %s gave %d, want %d", n.in, d.utcOffsetMin, n.want);
  }
  EXPECT(valid(d), "valid after all that");

  // refused: d must not change
  struct Bad { const char* key; const char* v; };
  const Bad bad[] = {
      {"foo", "1"}, {"", "1"}, {"Units", "mph"}, {"bluetoothBuilt", "true"}, {"setupDone", "true"}, {"rotation", "3"},
      {"units", "kph"}, {"units", "KMH"}, {"units", ""}, {"units", "mph "},
      {"upsideDown", "yes"}, {"upsideDown", "2"}, {"upsideDown", ""},
      {"brightness", "5"}, {"brightness", "9"}, {"brightness", "105"}, {"brightness", "101"}, {"brightness", "-10"},
      {"brightness", "abc"}, {"brightness", ""}, {"brightness", "50.5"}, {"brightness", "5 0"}, {"brightness", "50%"},
      {"brightness", "99999999999999"}, {"brightness", "-99999999999999"},
      {"compareTo", "week"}, {"compareTo", ""}, {"compareTo", "0"},
      {"bigNumber", "big"}, {"bigNumber", ""}, {"bigNumber", "Delta"},
      {"gpsMode", "20"}, {"gpsMode", "5hz"}, {"gpsMode", ""},
      {"bluetooth", "yes"}, {"bluetooth", ""}, {"bluetooth", "2"},
      {"tzMin", "841"}, {"tzMin", "-721"}, {"tzMin", "9999"}, {"tzMin", "abc"}, {"tzMin", ""}, {"tzMin", "5h"},
      {"recordRoadDrives", "yes"}, {"recordRoadDrives", ""},
  };
  for (const Bad& b : bad) {
    SettingsData x = randomSettings();
    const SettingsData before = x;
    EXPECT(!setField(x, b.key, b.v), "'%s' = '%s' must be refused", b.key, b.v);
    EXPECT(x == before, "'%s' = '%s' changed the settings anyway", b.key, b.v);
  }
  {
    SettingsData x = randomSettings();
    const SettingsData before = x;
    EXPECT(!setField(x, nullptr, "1") && !setField(x, "units", nullptr) && !setField(x, nullptr, nullptr), "null arguments");
    EXPECT(x == before, "null arguments changed the settings");
  }

  // parseBool and parseInt on their own
  bool b = true;
  EXPECT(!parseBool("yes", &b) && b, "parseBool yes leaves out alone");
  EXPECT(parseBool("off", &b) && !b, "parseBool off");
  EXPECT(parseBool("on", &b) && b, "parseBool on");
  int x = 123;
  EXPECT(!parseInt(nullptr, 0, 10, &x) && !parseInt("", 0, 10, &x) && !parseInt("11", 0, 10, &x) && !parseInt("-1", 0, 10, &x) &&
             !parseInt("5x", 0, 10, &x) && x == 123,
         "parseInt refusals must leave out alone, x = %d", x);
  EXPECT(parseInt("0", 0, 10, &x) && x == 0 && parseInt("10", 0, 10, &x) && x == 10 && parseInt("+5", 0, 10, &x) && x == 5 &&
             parseInt("-30", -720, 840, &x) && x == -30,
         "parseInt accepted values");

  // toJson -> setField restores the same settings
  testSeed(21);
  bool restored = true;
  int sent = 0, refused = 0;
  for (int i = 0; i < 300; i++) {
    SettingsData target = randomSettings();
    SettingsData start = randomSettings();
    start.setupDone = target.setupDone;  // not sent to the page
    char json[400];
    if (!toJson(target, json, sizeof(json))) restored = false;
    int rej = 0;
    sent = applyJson(start, json, &rej);
    refused = rej;
    restored = restored && start == target;
    if (start != target) printf("    round trip failed for %s\n", json);
  }
  EXPECT(restored, "toJson then setField must restore the settings");
  EXPECT(sent == 9 && refused == 1, "9 settings accepted and bluetoothBuilt refused, got %d and %d", sent, refused);
  printf("  toJson, setField (valid, refused, unchanged on failure), round trip\n");
}

// --- 6, 7, 8. what other code asks of a setting -----------------------------------------

static void testNeedsRestartAndGps() {
  for (int start = 0; start < 6; start++) {
    SettingsData a = start == 0 ? defaults() : randomSettings();
    for (int i = 0; i < SETTING_ITEM_COUNT; i++) {
      SettingsData b = a;
      adjust(b, itemAt(i), +1, true);
      EXPECT(needsRestart(a, b) == (itemAt(i) == SettingItem::Gps), "item %d: needsRestart %d", i, needsRestart(a, b));
    }
    EXPECT(!needsRestart(a, a), "same settings never need a restart");
  }
  SettingsData a = defaults(), b = defaults();
  a.setupDone = false;
  b.setupDone = true;
  b.rotation = a.rotation == 1 ? 3 : 1;
  b.units = a.units == SpeedUnits::Mph ? SpeedUnits::Kmh : SpeedUnits::Mph;
  EXPECT(!needsRestart(a, b), "setup answers apply at once");

  GpsSystems f = gpsSystems(GpsMode::Fast20), s = gpsSystems(GpsMode::AllSystems10);
  EXPECT(f.rateHz == 20 && f.galileo && !f.glonass && !f.beidou, "20 Hz: rate %d, galileo %d glonass %d beidou %d", f.rateHz,
         f.galileo, f.glonass, f.beidou);
  EXPECT(s.rateHz == 10 && s.galileo && s.glonass && s.beidou, "10 Hz: rate %d, galileo %d glonass %d beidou %d", s.rateHz,
         s.galileo, s.glonass, s.beidou);
  for (GpsSystems g : {f, s}) EXPECT(g.rateHz > 0 && 1000 % g.rateHz == 0, "1000 ms / %d Hz does not divide evenly", g.rateHz);
  EXPECT(1000 / f.rateHz == 50 && 1000 / s.rateHz == 100, "measurement periods %d and %d ms", 1000 / f.rateHz, 1000 / s.rateHz);
  printf("  needsRestart only for the GPS mode, GPS systems and rates\n");
}

static void testConversions() {
  EXPECT(speedValue(100.0f, SpeedUnits::Kmh) == 100.0f, "km/h unchanged");
  EXPECT(fabs(speedValue(100.0f, SpeedUnits::Mph) - 62.1371f) < 0.001, "100 km/h = %.4f mph", speedValue(100.0f, SpeedUnits::Mph));
  EXPECT(fabs(speedValue(160.934f, SpeedUnits::Mph) - 100.0f) < 0.001, "160.934 km/h = %.4f mph", speedValue(160.934f, SpeedUnits::Mph));
  EXPECT(speedValue(0.0f, SpeedUnits::Mph) == 0.0f, "0 stays 0");
  EXPECT(speedValue(-10.0f, SpeedUnits::Mph) < 0.0f, "negative stays negative");
  EXPECT(!strcmp(speedUnitLabel(SpeedUnits::Kmh), "km/h") && !strcmp(speedUnitLabel(SpeedUnits::Mph), "mph"), "unit labels");

  for (uint32_t max : {255u, 1023u, 8191u}) {
    uint32_t prev = 0;
    bool rising = true;
    for (int p = 10; p <= 100; p += 10) {
      uint32_t duty = backlightDuty((uint8_t)p, max);
      rising = rising && duty > prev;
      prev = duty;
    }
    EXPECT(rising, "duty must rise from 10%% to 100%% (max %u)", max);
    EXPECT(backlightDuty(100, max) == max, "100%% is max %u, got %u", max, backlightDuty(100, max));
    EXPECT(backlightDuty(50, max) < max / 2, "50%% (%u) must be below half of %u", backlightDuty(50, max), max);
    EXPECT(backlightDuty(0, max) == 1 && backlightDuty(10, max) >= 1, "never fully off (max %u)", max);
    EXPECT(backlightDuty(200, max) == max && backlightDuty(255, max) == max, "above 100%% is max");
  }
  EXPECT(backlightDuty(50, 1023) == 255, "50%% of 1023 is a quarter: %u", backlightDuty(50, 1023));

  const uint32_t H = 3600000, DAY = 86400000;
  EXPECT(localTimeOfDayMs(0, 0) == 0, "midnight UTC");
  EXPECT(localTimeOfDayMs(12 * H, -300) == 7 * H, "noon UTC at -5:00");
  EXPECT(localTimeOfDayMs(H, -300) == 20 * H, "01:00 UTC at -5:00 is the evening before");
  EXPECT(localTimeOfDayMs(0, -1) == DAY - 60000, "one minute west of midnight");
  EXPECT(localTimeOfDayMs(0, -720) == 12 * H, "UTC-12");
  EXPECT(localTimeOfDayMs(23 * H + 30 * 60000, 60) == 30 * 60000, "23:30 UTC at +1:00 is just after midnight");
  EXPECT(localTimeOfDayMs(DAY - 1, 840) == 14 * H - 1, "UTC+14 at the last ms of the day, got %u", localTimeOfDayMs(DAY - 1, 840));
  EXPECT(localTimeOfDayMs(10 * H, 330) == 15 * H + 30 * 60000, "India");
  EXPECT(localTimeOfDayMs(10 * H, 0) == 10 * H, "no offset");
  bool inDay = true;
  for (int off = -720; off <= 840; off += 15)
    for (uint32_t t : {0u, 1u, H, 12 * H, DAY - 1}) inDay = inDay && localTimeOfDayMs(t, (int16_t)off) < DAY;
  EXPECT(inDay, "local time of day is always within the day");
  printf("  speed units, backlight duty, local time of day\n");
}

static void testBigNumber() {
  for (BigNumber w : {BigNumber::Delta, BigNumber::Predicted, BigNumber::LapTime}) {
    EXPECT(bigNumberShown(w, true) == w, "with a delta the wanted number %d shows", (int)w);
    EXPECT(bigNumberShown(w, false) == BigNumber::LapTime, "without a delta %d falls back to the lap time", (int)w);
  }
  EXPECT(predictedLapMs(80000, 0) == 80000, "no delta change");
  EXPECT(predictedLapMs(80000, -1500) == 78500, "ahead of the best lap");
  EXPECT(predictedLapMs(80000, 2500) == 82500, "behind the best lap");
  EXPECT(predictedLapMs(1000, -5000) == 0, "a delta bigger than the best lap gives 0, not a huge number");
  EXPECT(predictedLapMs(80000, NO_DELTA) == 0, "the no-delta marker clamps to 0");
  EXPECT(predictedLapMs(0, 0) == 0 && predictedLapMs(0, 7) == 7, "zero best lap");
  EXPECT(predictedLapMs(600000, 100000) == 700000, "a long lap");
  printf("  big number fallbacks and predicted lap\n");
}

// --- 9. settings screen ----------------------------------------------------------------

static void testSettingsScreen() {
  using R = SettingsScreenState::Result;

  {  // Center walks the items, then the done page, then leaves
    SettingsScreenState st;
    SettingsData s = defaults();
    const SettingsData before = s;
    bool ok = true;
    for (int i = 0; i < SETTING_ITEM_COUNT; i++) {
      ok = ok && st.item == i && !st.donePage();
      ok = ok && st.handle(Key::Center, s) == R::Nothing;
    }
    EXPECT(ok, "Center walks item 0..%d", SETTING_ITEM_COUNT - 1);
    EXPECT(st.item == SETTING_ITEM_COUNT && st.donePage(), "then the done page, item %d", st.item);
    EXPECT(st.handle(Key::Center, s) == R::Exit, "Center on the done page exits");
    EXPECT(s == before, "walking must not change a value");
  }
  {  // Header and Hold leave from anywhere
    for (int i = 0; i <= SETTING_ITEM_COUNT; i++) {
      for (Key k : {Key::Header, Key::Hold}) {
        SettingsScreenState st;
        st.item = i;
        SettingsData s = defaults();
        EXPECT(st.handle(k, s) == R::Exit && s == defaults(), "item %d: key %d should exit", i, (int)k);
      }
    }
  }
  {  // BOOT short goes round, through the done page
    SettingsScreenState st;
    SettingsData s = defaults();
    bool ok = true;
    for (int i = 1; i <= SETTING_ITEM_COUNT + 1; i++) {
      ok = ok && st.handle(Key::BootShort, s) == R::Nothing && st.item == i % (SETTING_ITEM_COUNT + 1);
    }
    EXPECT(ok && st.item == 0, "BOOT short goes round %d pages, ended on %d", SETTING_ITEM_COUNT + 1, st.item);
    st.item = SETTING_ITEM_COUNT - 1;
    st.handle(Key::BootShort, s);
    EXPECT(st.donePage(), "BOOT short from the last item reaches the done page");
    EXPECT(s == defaults(), "BOOT short changes nothing");
  }
  {  // Left and Right on the done page go back into the list
    SettingsScreenState st;
    SettingsData s = defaults();
    st.item = SETTING_ITEM_COUNT;
    EXPECT(st.handle(Key::Left, s) == R::Nothing && st.item == SETTING_ITEM_COUNT - 1, "Left on done -> last item, got %d", st.item);
    st.item = SETTING_ITEM_COUNT;
    EXPECT(st.handle(Key::Right, s) == R::Nothing && st.item == 0, "Right on done -> first item, got %d", st.item);
    EXPECT(s == defaults(), "going back in changes nothing");
  }
  {  // Left and Right on items
    SettingsScreenState st;
    SettingsData s = defaults();  // brightness 100 at the top, compare "this session" at the end of its list
    st.item = (int)SettingItem::Brightness;
    EXPECT(s.brightness == 100, "test needs brightness 100");
    EXPECT(st.handle(Key::Right, s) == R::Nothing && s.brightness == 100 && st.item == 2, "Right at brightness 100");
    EXPECT(st.handle(Key::Left, s) == R::Changed && s.brightness == 90 && st.item == 2, "Left from 100");
    EXPECT(st.handle(Key::Right, s) == R::Changed && s.brightness == 100, "Right back to 100");
    for (int i = 0; i < 9; i++) EXPECT(st.handle(Key::Left, s) == R::Changed, "Left %d", i);
    EXPECT(s.brightness == 10 && st.handle(Key::Left, s) == R::Nothing && s.brightness == 10, "Left at 10 stops");

    st.item = (int)SettingItem::Compare;
    s.compareTo = CompareTo::Session;
    EXPECT(st.handle(Key::Right, s) == R::Nothing, "Right at the last compare choice");
    EXPECT(st.handle(Key::Left, s) == R::Changed && s.compareTo == CompareTo::Ever, "Left -> ever");
    EXPECT(st.handle(Key::Left, s) == R::Changed && s.compareTo == CompareTo::Day, "Left -> day");
    EXPECT(st.handle(Key::Left, s) == R::Nothing && s.compareTo == CompareTo::Day, "Left at the first compare choice");

    st.item = (int)SettingItem::Units;  // two choices: either key flips
    SpeedUnits u = s.units;
    EXPECT(st.handle(Key::Left, s) == R::Changed && s.units != u, "Left flips units");
    EXPECT(st.handle(Key::Right, s) == R::Changed && s.units == u, "Right flips units back");

    st.item = (int)SettingItem::TimeZone;
    s.utcOffsetMin = 840;
    EXPECT(st.handle(Key::Right, s) == R::Nothing && s.utcOffsetMin == 840, "Right at UTC+14");
    EXPECT(st.handle(Key::Left, s) == R::Changed && s.utcOffsetMin == 810, "Left from UTC+14");
    EXPECT(st.item == (int)SettingItem::TimeZone, "Left and Right keep the item");
  }
  {  // BOOT long changes with wrap, and leaves on the done page
    SettingsScreenState st;
    SettingsData s = defaults();
    st.item = (int)SettingItem::Brightness;
    EXPECT(st.handle(Key::BootLong, s) == R::Changed && s.brightness == 10, "BOOT long wraps 100 -> 10, got %d", s.brightness);
    for (int i = 0; i < SETTING_ITEM_COUNT; i++) {
      SettingsScreenState one;
      one.item = i;
      SettingsData x = defaults(), before = x;
      EXPECT(one.handle(Key::BootLong, x) == R::Changed && x != before && text(x, itemAt(i)) != text(before, itemAt(i)),
             "BOOT long on item %d must change it", i);
      EXPECT(one.item == i, "BOOT long keeps the item");
    }
    st.item = SETTING_ITEM_COUNT;
    SettingsData before = s;
    EXPECT(st.handle(Key::BootLong, s) == R::Exit && s == before, "BOOT long on the done page exits");
  }
  {  // Key::None does nothing, on an item or on the done page
    for (int i = 0; i <= SETTING_ITEM_COUNT; i++) {
      SettingsScreenState st;
      st.item = i;
      SettingsData s = defaults();
      EXPECT(st.handle(Key::None, s) == R::Nothing && st.item == i && s == defaults(), "Key::None at %d", i);
    }
  }
  printf("  settings screen: Center, Header/Hold, BOOT short/long, Left/Right\n");
}

// --- 10. first-boot setup ----------------------------------------------------------------

static void testSetup() {
  const uint8_t other = DISPLAY_ROTATION == 1 ? 3 : 1;
  {  // touch: left arrow = the screen as drawn is right way up, then right = mph
    SetupState st;
    SettingsData s = defaults();
    EXPECT(st.handle(Key::Left, s), "Left answers the first question");
    EXPECT(s.rotation == DISPLAY_ROTATION && !s.setupDone && st.step == 1, "rotation kept, step %d", st.step);
    EXPECT(st.handle(Key::Right, s), "Right answers the second");
    EXPECT(s.units == SpeedUnits::Mph && s.setupDone && s.rotation == DISPLAY_ROTATION, "mph, done");
  }
  {  // upside down: right flips, left = km/h (starting from mph so a change is seen)
    SetupState st;
    SettingsData s = defaults();
    s.units = SpeedUnits::Mph;
    EXPECT(st.handle(Key::Right, s) && s.rotation == other && st.step == 1 && !s.setupDone, "Right flips the screen");
    EXPECT(st.handle(Key::Left, s) && s.units == SpeedUnits::Kmh && s.setupDone && s.rotation == other, "Left = km/h, done");
  }
  {  // BOOT only
    SetupState st;
    SettingsData s = defaults();
    const SettingsData before = s;
    EXPECT(!st.handle(Key::BootShort, s) && st.highlight == 1 && s == before && st.step == 0, "BOOT short only moves the highlight");
    EXPECT(!st.handle(Key::BootShort, s) && st.highlight == 0, "BOOT short again goes back");
    st.handle(Key::BootShort, s);
    EXPECT(st.handle(Key::BootLong, s) && s.rotation == other && st.step == 1 && st.highlight == 0,
           "BOOT long on the right choice flips; step %d highlight %d", st.step, st.highlight);
    EXPECT(!s.setupDone, "one question to go");
    EXPECT(st.handle(Key::BootLong, s) && s.units == SpeedUnits::Kmh && s.setupDone, "BOOT long on the left choice = km/h, done");
  }
  {  // BOOT only, keep the screen and pick mph
    SetupState st;
    SettingsData s = defaults();
    EXPECT(st.handle(Key::BootLong, s) && s.rotation == DISPLAY_ROTATION && st.step == 1, "BOOT long keeps the screen");
    st.handle(Key::BootShort, s);
    EXPECT(st.handle(Key::BootLong, s) && s.units == SpeedUnits::Mph && s.setupDone, "BOOT short, long = mph");
  }
  {  // other keys are ignored on both steps
    for (int step = 0; step < 2; step++) {
      for (Key k : {Key::Center, Key::Header, Key::Hold, Key::None}) {
        SetupState st;
        st.step = step;
        st.highlight = 1;
        SettingsData s = defaults();
        EXPECT(!st.handle(k, s) && s == defaults() && st.step == step && st.highlight == 1, "step %d key %d ignored", step, (int)k);
      }
    }
  }
  {  // after the last answer nothing changes
    SetupState st;
    SettingsData s = defaults();
    st.handle(Key::Left, s);
    st.handle(Key::Right, s);
    const SettingsData done = s;
    for (Key k : {Key::Left, Key::Right, Key::Center, Key::Header, Key::Hold, Key::BootShort, Key::BootLong, Key::None}) {
      EXPECT(!st.handle(k, s) && s == done, "key %d after setup", (int)k);
    }
  }
  printf("  first-boot setup: touch, upside down, BOOT only, ignored keys\n");
}

// --- 11. end to end -------------------------------------------------------------------------

static const double SF_LAT = 37.0, SF_LON = -122.0, R = 150.0, PI_D = 3.14159265358979323846;

static void circlePos(double angle, double& lat, double& lon) {
  lat = SF_LAT + R * sin(angle) / 111194.93;
  lon = SF_LON + R * (cos(angle) - 1.0) / (111194.93 * cos(SF_LAT * geo::DEG2RAD));
}

static void testEndToEnd() {
  // First boot from the defaults, BOOT only: flip the screen, choose mph
  SettingsData s = defaults();
  SetupState setup;
  setup.handle(Key::BootShort, s);  // highlight the right-hand choice (upside down)
  setup.handle(Key::BootLong, s);
  setup.handle(Key::BootShort, s);  // mph
  setup.handle(Key::BootLong, s);
  EXPECT(s.setupDone && upsideDown(s) && s.units == SpeedUnits::Mph, "setup: done %d upside %d units %d", s.setupDone, upsideDown(s),
         (int)s.units);

  // Reboot: the stored copy comes back through sanitize
  SettingsData stored = s;
  sanitize(stored);
  EXPECT(stored == s && stored.setupDone, "the stored copy must come back unchanged and not ask again");
  EXPECT(stored.rotation == (DISPLAY_ROTATION == 1 ? 3 : 1) && stored.units == SpeedUnits::Mph, "choices kept");
  SettingsData rest = defaults();
  rest.setupDone = stored.setupDone;
  rest.rotation = stored.rotation;
  rest.units = stored.units;
  EXPECT(rest == stored, "everything else is still the defaults");
  char json[400];
  EXPECT(toJson(stored, json, sizeof(json)) > 0 && strstr(json, "\"units\":\"mph\"") && strstr(json, "\"upsideDown\":true"),
         "the Wi-Fi page sees the choices: %s", json);

  // Power cut after the first question: the second one is asked again
  {
    SettingsData half = defaults();
    SetupState st;
    st.handle(Key::Right, half);
    SettingsData back = half;
    sanitize(back);
    EXPECT(!back.setupDone && back.rotation == half.rotation, "half-finished setup asks again, keeps the screen");
  }

  // The settings screen, BOOT only: item 4 (big number) twice -> predicted
  SettingsScreenState screen;
  for (int i = 0; i < (int)SettingItem::Big; i++) screen.handle(Key::BootShort, s);
  screen.handle(Key::BootLong, s);
  screen.handle(Key::BootLong, s);
  EXPECT(s.bigNumber == BigNumber::Predicted, "big number set to predicted, got %d", (int)s.bigNumber);
  for (int i = (int)SettingItem::Big; i < SETTING_ITEM_COUNT; i++) screen.handle(Key::BootShort, s);
  EXPECT(screen.donePage() && screen.handle(Key::BootLong, s) == SettingsScreenState::Result::Exit, "leave from the done page");
  sanitize(s);
  EXPECT(s.bigNumber == BigNumber::Predicted && s.setupDone, "saved");

  // A session: lap 1 at 30 m/s, lap 2 at 27 m/s round a circle, GPS at 10 Hz
  LapTimer lt;
  lt.begin({SF_LAT, SF_LON, 15, 0, HeadingFrom::None}, {});
  const double T1 = 2 * PI_D * R / 30.0 * 1000.0, T2 = 2 * PI_D * R / 27.0 * 1000.0;
  double angle = -0.2;
  int checkedFixes = 0, lap1Fixes = 0;
  bool lapTimeBeforeBest = true, shownRight = true, closeEnough = true;
  double worst = 0;
  LapEvent lap2;
  for (int i = 0; i < 3000 && lt.lapCount() < 2; i++) {
    const double v = lt.lapCount() == 0 ? 30.0 : 27.0;
    double lat, lon;
    circlePos(angle, lat, lon);
    const uint32_t t = 3600000u + 100u * (uint32_t)i;
    for (const LapEvent& ev : lt.onFix(lat, lon, (float)(v * 3.6), t, t))
      if (ev.type == LapEventType::LapCompleted && lt.lapCount() == 2) lap2 = ev;

    const bool haveDelta = lt.liveDeltaMs() != NO_DELTA;
    if (lt.lapCount() == 0) {  // no best lap yet: whatever is wanted, the lap time shows
      lap1Fixes++;
      for (BigNumber w : {BigNumber::Delta, BigNumber::Predicted, BigNumber::LapTime})
        lapTimeBeforeBest = lapTimeBeforeBest && !haveDelta && bigNumberShown(w, haveDelta) == BigNumber::LapTime;
    } else if (lt.lapCount() == 1 && lt.lapRunning()) {
      const double f = (angle - 2 * PI_D) / (2 * PI_D);  // how far round lap 2 this fix is
      if (f > 0.05 && f < 0.95) {
        checkedFixes++;
        shownRight = shownRight && haveDelta && bigNumberShown(BigNumber::Delta, haveDelta) == BigNumber::Delta &&
                     bigNumberShown(s.bigNumber, haveDelta) == BigNumber::Predicted &&
                     bigNumberShown(BigNumber::LapTime, haveDelta) == BigNumber::LapTime;
        if (haveDelta) {
          const double want = T1 + f * (T2 - T1);
          const double got = predictedLapMs(lt.bestLap()->timeMs, lt.liveDeltaMs());
          worst = fmax(worst, fabs(got - want));
          closeEnough = closeEnough && fabs(got - want) <= 150;
        }
      }
    }
    angle += v * 0.1 / R;
  }
  EXPECT(lt.lapCount() == 2, "expected 2 laps, got %zu", lt.lapCount());
  EXPECT(lap1Fixes > 100 && lapTimeBeforeBest, "before a best lap exists the big number is the lap time (%d fixes)", lap1Fixes);
  EXPECT(checkedFixes > 100, "checked only %d fixes of lap 2", checkedFixes);
  EXPECT(shownRight, "during lap 2 delta and predicted must be shown as chosen");
  EXPECT(closeEnough, "predicted lap strayed %.0f ms from T1 + f * (T2 - T1)", worst);
  if (lt.lapCount() == 2) {
    EXPECT(lt.bestLapIndex() == 0 && fabs(lt.bestLap()->timeMs - T1) <= 2, "lap 1 is the best at %u ms (%.0f)", lt.bestLap()->timeMs, T1);
    // the speed changes one fix after the line, so lap 2 is a few ms short of the ideal
    EXPECT(fabs(lt.lap(1).timeMs - T2) <= 40, "lap 2 is %u ms (%.0f)", lt.lap(1).timeMs, T2);
    EXPECT(!lap2.isBest && fabs(lap2.deltaMs - (T2 - T1)) <= 60, "lap 2 delta %d ms, expected %.0f", lap2.deltaMs, T2 - T1);
    EXPECT(predictedLapMs(lt.bestLap()->timeMs, lap2.deltaMs) >= lt.lap(1).timeMs - 60, "predicted at the line is the lap time");
  }
  // in mph the lap-1 speed reads 67 mph
  EXPECT(fabs(speedValue(30.0f * 3.6f, s.units) - 67.108f) < 0.01, "30 m/s = %.2f mph", speedValue(30.0f * 3.6f, s.units));
  printf("  end to end: BOOT-only first boot, reboot, two laps (worst predicted error %.0f ms over %d fixes)\n", worst, checkedFixes);
}

int main() {
  printf("Settings tests\n");
  testDefaults();
  testSanitize();
  testAdjust();
  testValueText();
  testJson();
  testSetField();
  testNeedsRestartAndGps();
  testConversions();
  testBigNumber();
  testSettingsScreen();
  testSetup();
  testEndToEnd();
  printf("\n%d checks in 12 groups\n", checks);
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
