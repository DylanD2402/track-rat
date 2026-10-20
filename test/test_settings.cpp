/**
 * Host-side tests for the settings rules (include/SettingsData.h) and the
 * Settings list (layout, hit-testing, scrolling, touch and BOOT input) and
 * first-boot setup (include/SettingsFlow.h).
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
  d.deltaColours = (DeltaColours)(testRand() % 2);
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
  EXPECT(d.deltaColours == (DeltaColours)DEFAULT_DELTA_COLOURS, "deltaColours");
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
    d.deltaColours = (DeltaColours)v;
    sanitize(d);
    EXPECT(d.compareTo == def.compareTo && d.bigNumber == def.bigNumber && d.gpsMode == def.gpsMode &&
               d.deltaColours == def.deltaColours,
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
  for (SettingItem it : {SettingItem::Units, SettingItem::Upside, SettingItem::Colours, SettingItem::Gps, SettingItem::Bluetooth,
                         SettingItem::RoadDrives}) {
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
  d.deltaColours = DeltaColours::GreenRed;
  EXPECT(text(d, SettingItem::Colours) == "Green / red", "colours text");
  d.deltaColours = DeltaColours::BlueOrange;
  EXPECT(text(d, SettingItem::Colours) == "Blue / orange", "colours text");
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
  d.deltaColours = DeltaColours::BlueOrange;
  d.gpsMode = GpsMode::AllSystems10;
  d.bluetooth = false;
  d.utcOffsetMin = -330;
  d.recordRoadDrives = false;

  char buf[400];
  size_t len = toJson(d, buf, sizeof(buf));
  std::string want = std::string("{\"units\":\"mph\",\"upsideDown\":true,\"brightness\":70,\"compareTo\":\"ever\",") +
                     "\"bigNumber\":\"predicted\",\"deltaColours\":\"blueorange\",\"gpsMode\":\"10hz\",\"bluetooth\":false,\"tzMin\":-330," +
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
              std::count(j.begin(), j.end(), ',') == 10;
    for (const char* key : {"units", "upsideDown", "brightness", "compareTo", "bigNumber", "deltaColours", "gpsMode", "bluetooth", "tzMin",
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
  EXPECT(sent == 10 && refused == 1, "10 settings accepted and bluetoothBuilt refused, got %d and %d", sent, refused);
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

using ST = SettingsScreenState;
using Res = ST::Result;

// Screen x in the middle of the cards, y in the middle of row r (the row must be on screen)
static int midX() { return ST::CARD_X + ST::CARD_W / 2; }
static int midY(const ST& st, int r) { return st.rowScreenY(r) + ST::ROW_H / 2; }
static bool rowOnScreen(const ST& st, int r) {
  return st.rowScreenY(r) >= ST::TOP && st.rowScreenY(r) + ST::ROW_H <= st.viewBottom;
}
// Taps row r (scrolling to it first, as a finger would)
static Res tapRow(ST& st, int r, SettingsData& s) {
  st.setScroll(ST::rowTop(r) - 60);
  return st.tap(midX(), midY(st, r), s);
}
static int rowOf(SettingItem it) { return ST::rowOfItem(it); }

static void testSettingsLayout() {
  // Every setting is one row, in four sections, and then the Back row
  bool seen[SETTING_ITEM_COUNT] = {};
  int n = 0, lastKind = -1;
  for (int r = 0; r < ST::ROWS; r++) {
    const ST::Row& rw = ST::row(r);
    if (rw.kind == ST::RowKind::Back) {
      lastKind = r;
      continue;
    }
    EXPECT((int)rw.item >= 0 && (int)rw.item < SETTING_ITEM_COUNT && !seen[(int)rw.item], "row %d: item %d is listed twice or is not a setting", r, (int)rw.item);
    seen[(int)rw.item] = true;
    n++;
    EXPECT(ST::rowOfItem(rw.item) == r, "rowOfItem(%d)", (int)rw.item);
    EXPECT(strlen(ST::rowLabel(rw.item)) > 0, "row %d needs a label", r);
    // kinds: choices have options, toggles and levels do not
    EXPECT((rw.kind == ST::RowKind::Choice) == (ST::choiceCount(rw.item) > 0), "row %d: kind and choiceCount disagree", r);
  }
  EXPECT(n == SETTING_ITEM_COUNT && lastKind == ST::ROWS - 1, "all %d settings then Back: %d rows, Back at %d", SETTING_ITEM_COUNT, n, lastKind);

  // Sections hold the rows in order, each exactly once
  int next = 0;
  for (int i = 0; i < ST::SECTIONS; i++) {
    EXPECT(ST::section(i).first == next && ST::section(i).count > 0, "section %d starts at row %d", i, ST::section(i).first);
    for (int r = ST::section(i).first; r < ST::section(i).first + ST::section(i).count; r++) EXPECT(ST::sectionOf(r) == i, "row %d in section %d", r, i);
    next += ST::section(i).count;
  }
  EXPECT(next == ST::ROWS, "sections cover %d of %d rows", next, ST::ROWS);

  // Cards: rows stack inside, sections are apart, nothing is wider than the screen
  EXPECT(ST::CARD_X > 0 && ST::CARD_X + ST::CARD_W < ST::SCREEN_W, "the cards fit with a margin");
  EXPECT(ST::ROW_H >= 36, "rows must be big enough for a finger, %d", ST::ROW_H);
  for (int r = 1; r < ST::ROWS; r++) {
    const int gap = ST::rowTop(r) - ST::rowTop(r - 1) - ST::ROW_H;
    if (ST::sectionOf(r) == ST::sectionOf(r - 1)) EXPECT(gap == 0, "rows %d and %d touch, gap %d", r - 1, r, gap);
    else EXPECT(gap >= ST::SECTION_GAP, "sections of rows %d and %d are %d px apart", r - 1, r, gap);
  }
  EXPECT(ST::rowTop(0) >= ST::CAPTION_H, "the first caption fits above the first row");
  EXPECT(ST::contentHeight() >= ST::rowTop(ST::ROWS - 1) + ST::ROW_H, "content height covers the last row");

  // The list is taller than the screen, and the scroll range follows the view
  ST touch, boot;
  touch.begin(true);
  boot.begin(false);
  EXPECT(touch.viewBottom == ST::SCREEN_H && boot.viewBottom == ST::SCREEN_H - ST::HINT_H, "view bottom %d / %d", touch.viewBottom, boot.viewBottom);
  EXPECT(ST::contentHeight() > touch.viewH() && touch.maxScroll() == ST::contentHeight() - touch.viewH(), "the list must scroll: %d in %d", ST::contentHeight(), touch.viewH());
  EXPECT(boot.maxScroll() == touch.maxScroll() + ST::HINT_H, "the hint takes the bottom, so the list scrolls %d more", ST::HINT_H);
  EXPECT(touch.showHighlight == false && boot.showHighlight == true, "highlight is shown when there is no touch");

  // The scroll bar: at the top of the view at the top of the list, at the bottom at the bottom, always inside
  int y, h;
  EXPECT(touch.scrollBar(y, h) && y == ST::TOP && h >= 16 && h < touch.viewH(), "scroll bar at the top: y %d h %d", y, h);
  touch.setScroll(100000);
  EXPECT(touch.scrollBar(y, h) && y + h == touch.viewBottom, "scroll bar at the end: y %d h %d", y, h);
  touch.setScroll(touch.maxScroll() / 2);
  EXPECT(touch.scrollBar(y, h) && y > ST::TOP && y + h < touch.viewBottom, "scroll bar in the middle: y %d h %d", y, h);

  // The footnote is on the GPS section only
  int notes = 0;
  for (int i = 0; i < ST::SECTIONS; i++) notes += ST::section(i).foot != nullptr;
  EXPECT(notes == 1 && ST::section(ST::sectionOf(rowOf(SettingItem::Gps))).foot, "one footnote, under the GPS group");
  printf("  settings layout: %d rows in %d sections, %d px tall, scrolls %d\n", ST::ROWS, ST::SECTIONS, ST::contentHeight(), touch.maxScroll() );
}

static void testSettingsHitTesting() {
  for (int touchMode = 0; touchMode < 2; touchMode++) {
    ST st;
    st.begin(touchMode == 1);
    bool reached[ST::ROWS] = {};
    int mismatches = 0;
    for (int scroll = 0; scroll <= st.maxScroll(); scroll++) {
      st.setScroll(scroll);
      for (int y = 0; y < ST::SCREEN_H; y++) {
        for (int x : {ST::CARD_X, midX(), ST::CARD_X + ST::CARD_W - 1}) {
          const int hit = st.hitRow(x, y);
          // What the layout says should be there
          int want = -1;
          if (y >= ST::TOP && y < st.viewBottom)
            for (int r = 0; r < ST::ROWS; r++)
              if (y - ST::TOP + scroll >= ST::rowTop(r) && y - ST::TOP + scroll < ST::rowTop(r) + ST::ROW_H) want = r;
          if (hit != want) mismatches++;
          if (hit >= 0) reached[hit] = true;
        }
      }
    }
    EXPECT(mismatches == 0, "hit test disagrees with the layout %d times (touch %d)", mismatches, touchMode);
    bool all = true;
    for (int r = 0; r < ST::ROWS; r++) all = all && reached[r];
    EXPECT(all, "every row can be reached by scrolling (touch %d)", touchMode);
  }
  {  // Between cards, on captions, outside the card columns, in the top bar and in the hint: nothing
    ST st;
    st.begin(false);
    for (int scroll : {0, 40, st.maxScroll()}) {
      st.setScroll(scroll);
      for (int sec = 0; sec < ST::SECTIONS; sec++) {
        const int gapTop = ST::TOP + ST::cardBottom(sec) - scroll;
        const int gapEnd = sec + 1 < ST::SECTIONS ? ST::TOP + ST::cardTop(sec + 1) - scroll : gapTop + ST::SECTION_GAP;
        for (int y = gapTop; y < gapEnd; y++)
          if (y >= ST::TOP && y < st.viewBottom) EXPECT(st.hitRow(midX(), y) == -1, "y %d between cards (scroll %d) hit row %d", y, scroll, st.hitRow(midX(), y));
      }
      bool beside = true;
      for (int y = 0; y < ST::SCREEN_H; y++) beside = beside && st.hitRow(ST::CARD_X - 1, y) == -1 && st.hitRow(ST::CARD_X + ST::CARD_W, y) == -1;
      EXPECT(beside, "nothing beside the cards (scroll %d)", scroll);
      EXPECT(st.hitRow(midX(), ST::TOP - 1) == -1 && st.hitRow(midX(), 0) == -1, "the top bar is not a row");
      EXPECT(st.hitRow(midX(), st.viewBottom) == -1 && st.hitRow(midX(), ST::SCREEN_H - 1) == -1, "the hint is not a row");
    }
  }
  {  // taps: above y 30 is the top bar; below the view and between cards do nothing; the list scroll never moves
    ST st;
    st.begin(true);
    SettingsData s = defaults();
    st.setScroll(70);
    EXPECT(st.tap(midX(), 0, s) == Res::Exit && st.tap(midX(), ST::TOP - 1, s) == Res::Exit, "taps above y %d leave", ST::TOP);
    st.begin(true);
    st.setScroll(0);
    const int gapY = ST::TOP + ST::cardBottom(0) + 2;  // between DISPLAY and TIMING
    EXPECT(st.tap(midX(), gapY, s) == Res::Nothing && st.page == ST::Page::List && s == defaults(), "a tap between cards does nothing");
    EXPECT(st.tap(ST::CARD_X - 5, midY(st, 0), s) == Res::Nothing && st.page == ST::Page::List, "a tap beside the card does nothing");
    ST b;
    b.begin(false);
    EXPECT(b.tap(midX(), ST::SCREEN_H - 3, s) == Res::Nothing && b.page == ST::Page::List && s == defaults(), "a tap on the BOOT hint does nothing");
    EXPECT(b.tap(midX(), 5, s) == Res::Exit, "the top bar still works without touch hardware");
  }
  printf("  settings hit testing: every row reachable, gaps hit nothing, top bar above y %d\n", ST::TOP);
}

static void testSettingsScroll() {
  ST st;
  st.begin(true);
  const int mx = st.maxScroll();
  EXPECT(st.scroll == 0, "starts at the top");
  st.setScroll(-30);
  EXPECT(st.scroll == 0, "clamped at the top, %d", st.scroll);
  st.setScroll(mx + 500);
  EXPECT(st.scroll == mx, "clamped at the bottom, %d vs %d", st.scroll, mx);
  st.setScroll(0);

  // A drag reports how far the finger has moved in total; the content follows it
  st.drag(-30);
  EXPECT(st.scroll == 30, "finger up 30 -> content up 30, got %d", st.scroll);
  st.drag(-90);
  EXPECT(st.scroll == 90, "finger up 90 in all -> %d", st.scroll);
  st.drag(-20);
  EXPECT(st.scroll == 20, "finger comes back to 20 -> %d (the drag is relative to where it began)", st.scroll);
  st.drag(60);
  EXPECT(st.scroll == 0, "finger down past the top stops at 0, got %d", st.scroll);
  st.drag(-40);
  EXPECT(st.scroll == 40, "and follows again, %d", st.scroll);
  st.dragEnd();
  EXPECT(st.scroll == 40, "released: it stays, %d", st.scroll);
  st.drag(-10);  // a new drag starts from where the list is
  EXPECT(st.scroll == 50, "the next drag starts from 40: %d", st.scroll);
  st.drag(-100000);
  EXPECT(st.scroll == mx, "finger far up stops at the bottom, %d vs %d", st.scroll, mx);
  st.drag(100000);
  EXPECT(st.scroll == 0, "and far down at the top, %d", st.scroll);
  st.dragEnd();
  st.setScroll(mx);
  st.drag(-50);
  EXPECT(st.scroll == mx, "already at the bottom, pulling up stays");
  st.dragEnd();

  {  // a drag does nothing on a page
    ST p;
    p.begin(true);
    SettingsData s = defaults();
    tapRow(p, rowOf(SettingItem::Units), s);
    const int at = p.scroll;
    p.drag(-100);
    p.dragEnd();
    EXPECT(p.page == ST::Page::Choice && p.scroll == at, "dragging on a page leaves the list alone");
  }
  {  // drag, release, tap: the tap uses where the list ended up
    ST d;
    d.begin(true);
    SettingsData s = defaults();
    d.drag(-200);
    d.dragEnd();
    const int at = d.scroll;
    EXPECT(at == 200, "dragged to 200, got %d", at);
    int ty = 150;  // the first row at or below y 150 (it may be a section's caption)
    while (ty < 200 && d.hitRow(midX(), ty) < 0) ty++;
    int r = d.hitRow(midX(), ty);
    EXPECT(r >= 0 && d.rowScreenY(r) <= ty && ty < d.rowScreenY(r) + ST::ROW_H, "row %d is at y %d after the drag", r, ty);
    if (r < 0) return;
    EXPECT(d.tap(midX(), ty, s) != Res::Exit, "the tap is a row");
    EXPECT(d.scroll == at, "a tap does not scroll");
    const ST::Row& rw = ST::row(r);
    if (rw.kind == ST::RowKind::Toggle) EXPECT(s != defaults() && d.page == ST::Page::List, "the toggle under the finger flipped");
    else EXPECT(d.page != ST::Page::List && d.item == rw.item, "the page of the row under the finger opened");
  }
  printf("  settings scroll: clamped at both ends, drag follows the finger, release stays\n");
}

static void testSettingsPages() {
  {  // sub-pages keep the scroll of the list; the top bar goes back to it, the list's top bar to the menu
    for (int r = 0; r < ST::ROWS; r++) {
      const ST::Row& rw = ST::row(r);
      if (rw.kind == ST::RowKind::Toggle || rw.kind == ST::RowKind::Back) continue;
      for (int how = 0; how < 3; how++) {
        ST st;
        st.begin(true);
        SettingsData s = defaults();
        EXPECT(tapRow(st, r, s) == Res::Nothing && st.page != ST::Page::List && st.item == rw.item, "row %d opens its page", r);
        const int scroll = st.scroll;
        EXPECT(scroll > 0 || r < 3, "test needs a scrolled list for row %d", r);
        st.drag(-50);  // nothing on a page
        st.dragEnd();
        if (how == 0) EXPECT(st.tap(100, 5, s) == Res::Nothing, "page %d: the top bar goes back to the list", r);
        else if (how == 1) EXPECT(st.handle(Key::Header, s) == Res::Nothing, "page %d: Header goes back", r);
        else EXPECT(st.handle(Key::Hold, s) == Res::Nothing, "page %d: Hold goes back", r);
        EXPECT(st.page == ST::Page::List && st.scroll == scroll, "page %d: back on the list at scroll %d (was %d)", r, st.scroll, scroll);
        EXPECT(s == defaults(), "opening and closing a page changes nothing");
        // and from the list the top bar leaves
        EXPECT(st.handle(Key::Header, s) == Res::Exit && st.handle(Key::Hold, s) == Res::Exit, "the list's top bar and hold go back to the menu");
      }
    }
  }
  {  // switches flip on a tap, and flip back
    for (SettingItem it : {SettingItem::Upside, SettingItem::Bluetooth, SettingItem::RoadDrives}) {
      ST st;
      st.begin(true);
      SettingsData s = defaults();
      const bool was = ST::toggleOn(s, it);
      const int row = rowOf(it);
      EXPECT(ST::row(row).kind == ST::RowKind::Toggle, "row %d is a switch", row);
      EXPECT(tapRow(st, row, s) == Res::Changed && ST::toggleOn(s, it) == !was && st.page == ST::Page::List, "tap flips %d", (int)it);
      EXPECT(tapRow(st, row, s) == Res::Changed && ST::toggleOn(s, it) == was, "and back");
      SettingsData rest = defaults();
      EXPECT(s == rest, "only that one setting");
    }
    SettingsData s = defaults();
    ST st;
    st.begin(true);
    tapRow(st, rowOf(SettingItem::Upside), s);
    EXPECT(settings::upsideDown(s) && s.rotation != DISPLAY_ROTATION, "Screen upside down turns the screen");
  }
  {  // choice pages: tap an option, it is set and the list is back
    for (int r = 0; r < ST::ROWS; r++) {
      const ST::Row& rw = ST::row(r);
      if (rw.kind != ST::RowKind::Choice) continue;
      const int count = ST::choiceCount(rw.item);
      EXPECT(count >= 2 && count <= 3, "row %d has %d options", r, count);
      for (int start = 0; start < count; start++) {
        for (int pick = 0; pick < count; pick++) {
          ST st;
          st.begin(true);
          SettingsData s = defaults();
          ST::choiceSet(s, rw.item, start);
          const SettingsData before = s;
          EXPECT(tapRow(st, r, s) == Res::Nothing && st.page == ST::Page::Choice && st.choiceHl == start, "row %d opens on its current option %d", r, start);
          const int scroll = st.scroll;
          EXPECT(st.hitChoice(midX(), st.choiceRowY(pick) + 3) == pick && st.hitChoice(midX(), st.choiceRowY(pick) + ST::ROW_H - 1) == pick, "option %d is hit", pick);
          const Res res = st.tap(midX(), st.choiceRowY(pick) + ST::ROW_H / 2, s);
          EXPECT(res == (pick == start ? Res::Nothing : Res::Changed), "row %d: %d -> %d gives %d", r, start, pick, (int)res);
          EXPECT(ST::choiceIndex(s, rw.item) == pick, "row %d: option %d chosen, is %d", r, pick, ST::choiceIndex(s, rw.item));
          EXPECT(st.page == ST::Page::List && st.scroll == scroll && st.highlight == r, "back on the list, scroll %d, row %d", st.scroll, st.highlight);
          // nothing else changed
          SettingsData expect = before;
          ST::choiceSet(expect, rw.item, pick);
          EXPECT(s == expect, "only that setting changes");
          EXPECT(rowOnScreen(st, r), "the row just changed is on screen");
        }
      }
      ST st;
      st.begin(true);
      SettingsData s = defaults();
      tapRow(st, r, s);
      const int n = ST::choiceCount(rw.item);
      EXPECT(st.hitChoice(midX(), st.choiceRowY(n) + 1) == -1 && st.hitChoice(midX(), ST::CHOICE_TOP - 2) == -1 && st.hitChoice(ST::CARD_X - 2, st.choiceRowY(0) + 5) == -1, "outside the options hits none");
      EXPECT(st.tap(midX(), st.choiceRowY(n) + 12, s) == Res::Nothing && st.page == ST::Page::Choice && s == defaults(), "a tap below the options stays on the page");
      EXPECT(settings::itemNote(rw.item)[0] != 0, "a choice page has its note");
    }
  }
  printf("  settings pages: scroll kept, switches flip, choices select and return\n");
}

static void testSettingsLevels() {
  const int bright = rowOf(SettingItem::Brightness), zone = rowOf(SettingItem::TimeZone);
  {  // brightness: the bar, rounded to 10
    ST st;
    st.begin(true);
    SettingsData s = defaults();
    EXPECT(tapRow(st, bright, s) == Res::Nothing && st.page == ST::Page::Brightness, "brightness opens its page");
    const int y = ST::BAR_Y + ST::BAR_H / 2;
    struct { int x; int want; } cases[] = {
        {ST::BAR_X, 10},                        // the left end is the lowest level
        {ST::BAR_X - 10, 10},                   // beside the end still counts
        {ST::BAR_X + ST::BAR_W, 100},           // the right end
        {ST::BAR_X + ST::BAR_W + 9, 100},
        {ST::BAR_X + ST::BAR_W / 2, 50},        // the middle
        {ST::BAR_X + ST::BAR_W * 34 / 100, 30}, // 34% rounds down to 30
        {ST::BAR_X + ST::BAR_W * 36 / 100, 40}, // 36% rounds up to 40
        {ST::BAR_X + ST::BAR_W * 3 / 100, 10},  // 3% would be 0: the lowest is 10
        {ST::BAR_X + ST::BAR_W * 96 / 100, 100},
        {ST::BAR_X + ST::BAR_W * 70 / 100, 70},
    };
    for (const auto& c : cases) {
      s.brightness = c.want == 60 ? 50 : 60;  // never the wanted level, so Changed is expected
      EXPECT(st.tap(c.x, y, s) == Res::Changed && s.brightness == c.want, "bar at x %d should give %d, got %d", c.x, c.want, s.brightness);
      EXPECT(s.brightness % 10 == 0 && s.brightness >= 10 && s.brightness <= 100, "valid level %d", s.brightness);
      EXPECT(ST::barLevel(c.x) == c.want, "barLevel(%d) = %d", c.x, ST::barLevel(c.x));
    }
    s.brightness = 50;
    EXPECT(st.tap(ST::BAR_X + ST::BAR_W / 2, y, s) == Res::Nothing && s.brightness == 50, "the level you already have is not a change");
    EXPECT(st.tap(ST::BAR_X + 100, ST::BAR_Y + ST::BAR_H + ST::BAR_HIT + 2, s) == Res::Nothing, "below the bar is not the bar");
    EXPECT(st.tap(ST::BAR_X + 100, ST::BAR_Y - ST::BAR_HIT - 2, s) == Res::Nothing, "above the bar is not the bar");
    EXPECT(s.brightness == 50 && st.page == ST::Page::Brightness, "still on the page");
    // every pixel along the bar gives a level in range, going up with x
    int last = 0;
    bool ok = true;
    for (int x = ST::BAR_X - 10; x <= ST::BAR_X + ST::BAR_W + 10; x++) {
      const int v = ST::barLevel(x);
      ok = ok && v >= last && v >= 10 && v <= 100 && v % 10 == 0;
      last = v;
    }
    EXPECT(ok, "bar levels rise with x, 10 to 100 in tens");

    // - and +: steps of 10, stop at the ends
    s.brightness = 100;
    EXPECT(st.tap(ST::PLUS_X + 5, ST::BTN_Y + 5, s) == Res::Nothing && s.brightness == 100, "+ at 100 stops");
    EXPECT(st.tap(ST::MINUS_X + 5, ST::BTN_Y + 5, s) == Res::Changed && s.brightness == 90, "- from 100");
    EXPECT(st.tap(ST::PLUS_X + ST::BTN_W - 1, ST::BTN_Y + ST::BTN_H - 1, s) == Res::Changed && s.brightness == 100, "+ back");
    for (int i = 0; i < 9; i++) EXPECT(st.tap(ST::MINUS_X + 70, ST::BTN_Y + 20, s) == Res::Changed, "- %d", i);
    EXPECT(s.brightness == 10 && st.tap(ST::MINUS_X + 70, ST::BTN_Y + 20, s) == Res::Nothing && s.brightness == 10, "- at 10 stops");
    EXPECT(st.page == ST::Page::Brightness, "buttons keep the page");
    EXPECT(st.tap(ST::MINUS_X + ST::BTN_W + 2, ST::BTN_Y + 20, s) == Res::Nothing && s.brightness == 10, "between the buttons is nothing");
    EXPECT(!ST::onMinus(ST::MINUS_X - 1, ST::BTN_Y) && !ST::onPlus(ST::PLUS_X + ST::BTN_W, ST::BTN_Y), "button edges");
    EXPECT(ST::MINUS_X + ST::BTN_W < ST::PLUS_X && ST::PLUS_X + ST::BTN_W <= ST::SCREEN_W && ST::BTN_Y + ST::BTN_H <= ST::SCREEN_H - ST::HINT_H, "buttons fit the screen");
    EXPECT(ST::BAR_Y + ST::BAR_H + ST::BAR_HIT < ST::BTN_Y, "the bar's touch area stays off the buttons");
  }
  {  // time zone: 30 minute steps, stops at the ends, nothing else changes
    ST st;
    st.begin(true);
    SettingsData s = defaults();
    EXPECT(tapRow(st, zone, s) == Res::Nothing && st.page == ST::Page::TimeZone, "time zone opens its page");
    EXPECT(st.tap(ST::PLUS_X + 10, ST::BTN_Y + 10, s) == Res::Changed && s.utcOffsetMin == 30, "+ is half an hour");
    EXPECT(st.tap(ST::MINUS_X + 10, ST::BTN_Y + 10, s) == Res::Changed && s.utcOffsetMin == 0, "- is half an hour");
    EXPECT(st.tap(ST::MINUS_X + 10, ST::BTN_Y + 10, s) == Res::Changed && s.utcOffsetMin == -30, "below zero");
    s.utcOffsetMin = settings::UTC_MAX;
    EXPECT(st.tap(ST::PLUS_X + 10, ST::BTN_Y + 10, s) == Res::Nothing && s.utcOffsetMin == settings::UTC_MAX, "+ at UTC+14 stops");
    s.utcOffsetMin = settings::UTC_MIN;
    EXPECT(st.tap(ST::MINUS_X + 10, ST::BTN_Y + 10, s) == Res::Nothing && s.utcOffsetMin == settings::UTC_MIN, "- at UTC-12 stops");
    int steps = 0;
    while (st.tap(ST::PLUS_X + 10, ST::BTN_Y + 10, s) == Res::Changed) steps++;
    EXPECT(steps == (settings::UTC_MAX - settings::UTC_MIN) / settings::UTC_STEP && s.utcOffsetMin == settings::UTC_MAX, "%d steps to the other end", steps);
    EXPECT(text(s, SettingItem::TimeZone) == "UTC+14:00", "reads %s", text(s, SettingItem::TimeZone).c_str());
    s.utcOffsetMin = -270;
    EXPECT(text(s, SettingItem::TimeZone) == "UTC" MINUS "4:30", "reads %s", text(s, SettingItem::TimeZone).c_str());
    EXPECT(st.tap(ST::BAR_X + 50, ST::BAR_Y + 3, s) == Res::Nothing && s.utcOffsetMin == -270, "there is no bar on the time zone page");
    SettingsData rest = defaults();
    rest.utcOffsetMin = -270;
    EXPECT(s == rest, "only the time zone changed");
  }
  printf("  settings levels: brightness bar rounds to 10, - and + clamp, time zone steps by 30\n");
}

static void testSettingsBoot() {
  {  // the highlight walks every row and wraps, and the list scrolls to keep it in view
    ST st;
    st.begin(false);
    SettingsData s = defaults();
    EXPECT(st.highlight == 0 && st.showHighlight && rowOnScreen(st, 0), "starts on the first row");
    bool ok = true, moved = false;
    for (int i = 1; i <= 3 * ST::ROWS; i++) {
      const int prev = st.scroll;
      ok = ok && st.handle(Key::BootShort, s) == Res::Nothing && st.highlight == i % ST::ROWS && rowOnScreen(st, st.highlight);
      ok = ok && st.scroll >= 0 && st.scroll <= st.maxScroll() && s == defaults() && st.page == ST::Page::List;
      moved = moved || st.scroll != prev;
    }
    EXPECT(ok, "BOOT short goes round the %d rows and keeps the row in view", ST::ROWS);
    EXPECT(moved && st.highlight == 0 && st.scroll == 0, "the list scrolled along, and is back at the top after the wrap (scroll %d)", st.scroll);
    // going to the first row of a section shows its caption; the last row shows the footnote
    for (int i = 0; i < ST::ROWS; i++) {
      st.handle(Key::BootShort, s);
      const int h = st.highlight;
      const ST::Section& sc = ST::section(ST::sectionOf(h));
      if (h == sc.first && sc.title) EXPECT(st.rowScreenY(h) - ST::CAPTION_H >= ST::TOP, "the caption of row %d is in view", h);
      if (h == sc.first + sc.count - 1 && sc.foot) EXPECT(st.rowScreenY(h) + ST::ROW_H + ST::FOOT_H <= st.viewBottom, "the footnote under row %d is in view", h);
    }
    st.setScroll(0);
    st.highlight = ST::ROWS - 1;
    st.handle(Key::BootShort, s);
    st.handle(Key::BootShort, s);
    EXPECT(st.highlight == 1, "wraps to the second row");
  }
  {  // BOOT works with a touch screen too: the first press shows the highlight
    ST st;
    st.begin(true);
    SettingsData s = defaults();
    EXPECT(!st.showHighlight, "no highlight while touching");
    st.handle(Key::BootShort, s);
    EXPECT(st.showHighlight && st.highlight == 1, "BOOT shows it");
    st.tap(midX(), midY(st, 1), s);
    EXPECT(!st.showHighlight, "a touch hides it again");
  }
  {  // hold on every row does what a tap would
    for (int target = 0; target < ST::ROWS; target++) {
      ST st;
      st.begin(false);
      SettingsData s = defaults();
      for (int i = 0; i < target; i++) st.handle(Key::BootShort, s);
      const ST::Row& rw = ST::row(target);
      const Res r = st.handle(Key::BootLong, s);
      if (rw.kind == ST::RowKind::Back) {
        EXPECT(r == Res::Exit && s == defaults(), "hold on Back leaves");
      } else if (rw.kind == ST::RowKind::Toggle) {
        EXPECT(r == Res::Changed && ST::toggleOn(s, rw.item) != ST::toggleOn(defaults(), rw.item) && st.page == ST::Page::List, "hold flips row %d", target);
      } else {
        EXPECT(r == Res::Nothing && st.page != ST::Page::List && st.item == rw.item && s == defaults(), "hold opens row %d", target);
        if (rw.kind == ST::RowKind::Choice) EXPECT(st.choiceHl == ST::choiceIndex(s, rw.item), "a choice page starts on the current option");
      }
    }
  }
  {  // choice pages: press walks the options and wraps, hold chooses and goes back
    for (int r = 0; r < ST::ROWS; r++) {
      const ST::Row& rw = ST::row(r);
      if (rw.kind != ST::RowKind::Choice) continue;
      const int count = ST::choiceCount(rw.item);
      for (int steps = 0; steps <= count; steps++) {
        ST st;
        st.begin(false);
        SettingsData s = defaults();
        for (int i = 0; i < r; i++) st.handle(Key::BootShort, s);
        st.handle(Key::BootLong, s);
        const int start = st.choiceHl;
        for (int i = 0; i < steps; i++) EXPECT(st.handle(Key::BootShort, s) == Res::Nothing && s == defaults(), "moving the highlight changes nothing");
        EXPECT(st.choiceHl == (start + steps) % count, "row %d: %d presses from %d -> %d", r, steps, start, st.choiceHl);
        const int scroll = st.scroll;
        const Res res = st.handle(Key::BootLong, s);
        const bool same = (start + steps) % count == start;
        EXPECT(res == (same ? Res::Nothing : Res::Changed) && ST::choiceIndex(s, rw.item) == (start + steps) % count, "row %d: hold chooses option %d", r, (start + steps) % count);
        EXPECT(st.page == ST::Page::List && st.highlight == r && st.scroll == scroll && rowOnScreen(st, r), "and goes back to the row");
      }
    }
  }
  {  // level pages: press is + and wraps, hold goes back
    ST st;
    st.begin(false);
    SettingsData s = defaults();
    const int bright = rowOf(SettingItem::Brightness), zone = rowOf(SettingItem::TimeZone);
    for (int i = 0; i < bright; i++) st.handle(Key::BootShort, s);
    st.handle(Key::BootLong, s);
    EXPECT(st.page == ST::Page::Brightness, "opened brightness");
    EXPECT(st.handle(Key::BootShort, s) == Res::Changed && s.brightness == 10, "100 wraps to 10, got %d", s.brightness);
    EXPECT(st.handle(Key::BootShort, s) == Res::Changed && s.brightness == 20, "then 20");
    for (int i = 0; i < 8; i++) st.handle(Key::BootShort, s);
    EXPECT(s.brightness == 100, "ten presses go round, %d", s.brightness);
    EXPECT(st.handle(Key::BootLong, s) == Res::Nothing && st.page == ST::Page::List && st.highlight == bright, "hold goes back");
    for (int i = bright; i != zone; i = (i + 1) % ST::ROWS) st.handle(Key::BootShort, s);
    st.handle(Key::BootLong, s);
    EXPECT(st.page == ST::Page::TimeZone, "opened the time zone");
    s.utcOffsetMin = settings::UTC_MAX;
    EXPECT(st.handle(Key::BootShort, s) == Res::Changed && s.utcOffsetMin == settings::UTC_MIN, "UTC+14 wraps to UTC-12");
    EXPECT(st.handle(Key::BootShort, s) == Res::Changed && s.utcOffsetMin == settings::UTC_MIN + 30, "then half an hour on");
    EXPECT(st.handle(Key::BootLong, s) == Res::Nothing && st.page == ST::Page::List && st.highlight == zone, "hold goes back");
  }
  {  // a full BOOT-only visit: every row, every page; nothing is missed, nothing is changed except on purpose
    ST st;
    st.begin(false);
    SettingsData s = defaults();
    bool opened[SETTING_ITEM_COUNT] = {};
    for (int r = 0; r < ST::ROWS - 1; r++) {
      const ST::Row& rw = ST::row(st.highlight);
      EXPECT(st.highlight == r, "visiting row %d", r);
      if (rw.kind == ST::RowKind::Toggle) {
        st.handle(Key::BootLong, s);
        st.handle(Key::BootLong, s);  // flip and flip back
      } else {
        st.handle(Key::BootLong, s);
        opened[(int)st.item] = st.page != ST::Page::List;
        st.handle(Key::Hold, s);  // the hold also backs out of a page
        EXPECT(st.page == ST::Page::List, "back from row %d", r);
      }
      st.handle(Key::BootShort, s);
    }
    int pages = 0;
    for (int i = 0; i < SETTING_ITEM_COUNT; i++) pages += opened[i];
    EXPECT(pages == 7, "visited 7 pages (5 choices, brightness, time zone), got %d", pages);
    EXPECT(s == defaults(), "a look round changes nothing");
    EXPECT(st.highlight == ST::ROWS - 1 && rowOnScreen(st, st.highlight) && st.scroll == st.maxScroll(), "ended on Back at the bottom");
    EXPECT(st.handle(Key::BootLong, s) == Res::Exit, "hold on Back leaves");
  }
  {  // Key::None, Left, Right, Center do nothing; nothing outside a page can leave a bad state
    for (Key k : {Key::None, Key::Left, Key::Center, Key::Right}) {
      ST st;
      st.begin(true);
      SettingsData s = defaults();
      EXPECT(st.handle(k, s) == Res::Nothing && s == defaults() && st.page == ST::Page::List && st.scroll == 0, "key %d does nothing", (int)k);
    }
  }
  printf("  settings BOOT: highlight walks and wraps, hold opens/flips/chooses, pages wrap\n");
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

  // The settings screen, BOOT only: open Big number, move to Predicted, choose it, then walk down to Back and leave
  SettingsScreenState screen;
  screen.begin(false);
  for (int i = 0; i < screen.rowOfItem(SettingItem::Big); i++) screen.handle(Key::BootShort, s);
  screen.handle(Key::BootLong, s);
  for (int i = 0; i < 3 && screen.choiceHl != (int)BigNumber::Predicted; i++) screen.handle(Key::BootShort, s);
  EXPECT(screen.handle(Key::BootLong, s) == SettingsScreenState::Result::Changed, "choosing a different big number is a change");
  EXPECT(s.bigNumber == BigNumber::Predicted, "big number set to predicted, got %d", (int)s.bigNumber);
  for (int i = screen.highlight; i < SettingsScreenState::ROWS - 1; i++) screen.handle(Key::BootShort, s);
  EXPECT(screen.highlight == SettingsScreenState::ROWS - 1 && screen.handle(Key::BootLong, s) == SettingsScreenState::Result::Exit, "leave from the Back row");
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
  testSettingsLayout();
  testSettingsHitTesting();
  testSettingsScroll();
  testSettingsPages();
  testSettingsLevels();
  testSettingsBoot();
  testSetup();
  testEndToEnd();
  printf("\n%d checks in 17 groups\n", checks);
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
