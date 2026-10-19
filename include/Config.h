/**
 * Track Rat - Configuration
 *
 * Board: LCDWiki ES3C28P (ESP32-S3 N16R8, 2.8" ILI9341V IPS, FT6336G touch, SDIO SD slot)
 * GPS:   Beitian BE-252Q (UART: binary UBX-NAV-PVT, or NMEA from other modules)
 *
 * Display pins are passed to TFT_eSPI from platformio.ini build_flags.
 */

#ifndef CONFIG_H
#define CONFIG_H

// ---------------------------------------------------------------------------
// GPS (Beitian BE-252Q)
// Plug it into the board's UART connector (GPIO 43/44) or the expansion
// header (GPIO 14/21). At boot every pair below is tried, both ways round,
// so TX/RX orientation doesn't matter. {ESP32 RX pin, ESP32 TX pin}
// ---------------------------------------------------------------------------
#define GPS_PIN_PAIRS {{44, 43}, {43, 44}, {14, 21}, {21, 14}}
#define GPS_DEFAULT_BAUD 115200      // BE-252Q factory default
// Update rate vs satellite systems (u-blox M10 maximums): 4 systems 10 Hz,
// 3: 16 Hz, 2: 20 Hz, GPS only: 25 Hz. More systems = more satellites = more
// accurate fixes; faster = smoother map and live diff. 20 Hz + GPS/Galileo
// is the usual lap-timer choice. These are the GPS mode setting's "20 Hz"
// (its default when GPS_TARGET_RATE_HZ is 20 or more); its other choice,
// 10 Hz with all four systems, is for tracks lined with trees.
#define GPS_TARGET_RATE_HZ 20
#define GPS_SET_CONSTELLATIONS 1     // 0 = leave the module's satellite systems alone
#define GPS_USE_GALILEO 1
#define GPS_USE_GLONASS 0
#define GPS_USE_BEIDOU 0
#define GPS_CONFIGURE_UBLOX 1        // send UBX config (RAM only, never saved to the module)
#define GPS_USE_UBX 1                // u-blox: read binary UBX-NAV-PVT instead of NMEA (0 = always NMEA)
#define GPS_PVT_WAIT_MS 3000         // no NAV-PVT this long after asking = not u-blox, stay on NMEA
#define GPS_ECHO_NMEA 0              // 1 = copy raw NMEA to the USB serial monitor
#define GPS_RX_BUFFER_BYTES 16384     // ~5 s of NMEA at 20 Hz
#define GPS_MIN_SATELLITES 4
#define GPS_FIX_TIMEOUT_MS 3000      // no fresh fix for this long = fix lost

// ---------------------------------------------------------------------------
// Bluetooth: stream GPS to phone apps as a RaceBox Mini (RaceChrono etc.)
// ---------------------------------------------------------------------------
#ifndef BLE_ENABLED
#define BLE_ENABLED 1
#endif

// ---------------------------------------------------------------------------
// Settings: changed on the device (Menu -> Settings) or the Wi-Fi page, and
// kept in flash. These are what a new unit starts with. Rotation, GPS mode
// and Bluetooth take theirs from DISPLAY_ROTATION, GPS_TARGET_RATE_HZ and
// BLE_ENABLED (0 leaves Bluetooth out of the build). Pins stay in this file.
// ---------------------------------------------------------------------------
#define DEFAULT_SPEED_MPH 0          // 0 = km/h, 1 = mph (also on the Performance screens)
#define DEFAULT_BRIGHTNESS_PCT 100   // 10..100
#define DEFAULT_COMPARE_TO 2         // 0 = best of the day, 1 = best ever, 2 = this session
#define DEFAULT_BIG_NUMBER 2         // 0 = delta, 1 = predicted lap, 2 = lap time
#define DEFAULT_UTC_OFFSET_MIN 0     // minutes east of UTC, for file names
#define DEFAULT_RECORD_ROAD_DRIVES 1 // 0 = Free Roam drives aren't saved

// ---------------------------------------------------------------------------
// Display (ILI9341V 240x320, used in landscape 320x240)
// ---------------------------------------------------------------------------
#define DISPLAY_ROTATION 1           // 1 or 3 for landscape: "right way up". The Screen setting's
                                     // "upside down" uses the other one; the touch mapping below is for this one
#define BACKLIGHT_PWM_CHANNEL 7      // LEDC channel dimming the backlight pin (TFT_BL, IO45)
#define BACKLIGHT_PWM_HZ 5000
#define BACKLIGHT_PWM_BITS 10
#define DISPLAY_INVERT 1             // IPS panel: if the background shows WHITE, set to 0
#define DISPLAY_FPS 10
#define SPLASH_ENABLED 1             // logo animation at start-up (BOOT skips it)
#define SPLASH_FADE_MS 1000          // each sweep (in, then out)
#define SPLASH_HOLD_MS 700
#define DISPLAY_PARTIAL_UPDATE 1     // 1 = send only changed rows (less tearing), 0 = send every frame whole
#define DISPLAY_TEST_PATTERN 0       // 1 = show 4 coloured stripes for 2 s at boot (red, green, blue, white)

// ---------------------------------------------------------------------------
// Touch (FT6336G, I2C)
// ---------------------------------------------------------------------------
#define TOUCH_SDA 16
#define TOUCH_SCL 15
#define TOUCH_INT 17
#define TOUCH_RST 18
#define TOUCH_I2C_ADDR 0x38
// Raw touch is in portrait panel coordinates. Adjust these if taps land in the
// wrong place (the serial monitor logs raw + mapped coordinates for each tap).
#define TOUCH_SWAP_XY 1
#define TOUCH_INVERT_X 0
#define TOUCH_INVERT_Y 1

// BOOT button (GPIO0): short press = next, long press = select / confirm
#define BOOT_BUTTON_PIN 0
#define LONG_PRESS_MS 1000
#define END_SESSION_HOLD_MS 2000     // hold (touch or BOOT) to end a running session

// ---------------------------------------------------------------------------
// Optional add-ons on the expansion header (see README, "Big button and LED bar").
// Each add-on is switched off at boot if the GPS was found on its pin.
// ---------------------------------------------------------------------------
#define EXT_BUTTON_ENABLED 1         // momentary push button, IO21 to GND; same as BOOT
#define EXT_BUTTON_PIN 21
#define LED_BAR_ENABLED 1            // WS2812B strip delta bar
#define LED_BAR_PIN 2
#define LED_BAR_COUNT 10             // LEDs on the strip (2 to 64)
#define LED_BAR_BRIGHTNESS 64        // 0-255; a full strip at 255 can draw over 500 mA
#define LED_BAR_REVERSED 0           // 1 = flip the bar if "ahead" grows to the left
#define LED_BAR_FULL_SCALE_MS 1000   // the whole bar side lights at this much delta
#define BOARD_LED_ENABLED 1          // mirror the delta colour on the board's own WS2812
#define BOARD_LED_PIN 42
#define BOARD_LED_BRIGHTNESS 40      // 0-255

// ---------------------------------------------------------------------------
// SD card (SDIO 4-bit)
// ---------------------------------------------------------------------------
#define SD_CLK 38
#define SD_CMD 40
#define SD_D0 39
#define SD_D1 41
#define SD_D2 48
#define SD_D3 47

// ---------------------------------------------------------------------------
// Lap timing
// ---------------------------------------------------------------------------
#define TRACK_DETECTION_RADIUS_M 5000.0  // auto-select a track if its S/F is this close
#define DEFAULT_GATE_HALF_WIDTH_M 15.0   // used when a track has no "radius"
#define MIN_LAP_TIME_MS 20000            // S/F crossings sooner than this are ignored
#define MAX_LAP_TIME_MS 900000           // longer laps (pit stops) aren't recorded
#define MIN_CROSSING_SPEED_KMH 5.0       // ignore jitter while parked on the line
#define MAX_FIX_GAP_MS 2000              // don't interpolate across GPS dropouts
#define MAX_LAPS_STORED 500
#define MAX_TRACE_POINTS 30000           // path points kept per lap for live diff (50 min at 10 Hz)
#define LIVE_DELTA_MAX_OFFSET_M 40.0     // further than this from the best lap's line = no live diff

// ---------------------------------------------------------------------------
// Performance runs (acceleration and braking)
// ---------------------------------------------------------------------------
#define PERF_ROLLOUT_DEFAULT 0           // 1 = start the clock after 1 foot of rollout (drag-strip style)

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------
#define TRACK_DATABASE_FILE "/tracks.json"
#define SESSION_DIR "/sessions"
// Full-rate session logs (.vbo, SD card only): every fix is buffered in PSRAM
// and written from core 0 about once a second
#define LOG_BUFFER_BYTES (256 * 1024)    // power of two; ~2 min of 20 Hz fixes
#define LOG_FLUSH_MS 1000

// ---------------------------------------------------------------------------
// Battery (3.7 V LiPo on the board's socket, TP4054 charger, voltage on IO9)
// The board divides the battery voltage by 2 (two 200 k resistors) before IO9.
// ---------------------------------------------------------------------------
#define BATTERY_ENABLED 1
#define BATTERY_ADC_PIN 9
#define BATTERY_DIVIDER_RATIO 2.0f   // 200 k + 200 k
#define BATTERY_CAL_OFFSET_MV 0      // trim against a multimeter on the battery (+ reads higher)
#define BATTERY_SAMPLES 16           // ADC readings averaged each time
#define BATTERY_READ_MS 1000         // how often the voltage is read
// No battery fitted: the pin floats low, or the charger holds the empty socket at its float voltage.
#define BATTERY_PRESENT_MIN_MV 2500
#define BATTERY_PRESENT_MAX_MV 4230
#define BATTERY_PRESENT_HYST_MV 30
#define BATTERY_ABSENT_SAMPLES 5     // this many bad readings in a row (1 s apart) = no battery
#define BATTERY_LOW_MV 3300          // save the session and sleep at or below this...
#define BATTERY_LOW_HOLD_MS 10000    // ...for this long
#define BATTERY_LOW_SCREEN_MS 5000   // how long the "battery low" message shows before sleeping

// ---------------------------------------------------------------------------
// Auto-sleep: parked with no touch or BOOT press. Waking is by BOOT (IO0) or a touch (INT, IO17).
// ---------------------------------------------------------------------------
#define AUTO_SLEEP_ENABLED 1
#define AUTO_SLEEP_IDLE_MS (20UL * 60UL * 1000UL)  // parked and untouched this long: backlight off
#define AUTO_SLEEP_DEEP_AFTER_MS (60UL * 1000UL)   // backlight off this long: deep sleep
#define AUTO_SLEEP_MOVING_KMH 5.0                  // at or above this speed the car counts as moving

// ---------------------------------------------------------------------------
// Firmware update from the SD card
// ---------------------------------------------------------------------------
#define FIRMWARE_FILE "/trackrat.bin"
#define FIRMWARE_DONE_FILE "/trackrat.installed.bin"  // the file is renamed to this after a successful install
#define FIRMWARE_PROMPT_TIMEOUT_MS 30000              // no answer in this time = skip

// ---------------------------------------------------------------------------
// Debug output (USB serial)
// ---------------------------------------------------------------------------
#ifndef DEBUG_MODE
#define DEBUG_MODE 1
#endif

#if DEBUG_MODE
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(fmt, ...)
#endif

#endif  // CONFIG_H
