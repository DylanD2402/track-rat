/**
 * Track Rat - Configuration
 *
 * Board: LCDWiki ES3C28P (ESP32-S3 N16R8, 2.8" ILI9341V IPS, FT6336G touch, SDIO SD slot)
 * GPS:   Beitian BE-252Q (UART, NMEA)
 *
 * Display pins are passed to TFT_eSPI from platformio.ini build_flags.
 */

#ifndef CONFIG_H
#define CONFIG_H

// ---------------------------------------------------------------------------
// GPS (Beitian BE-252Q) - wired to the expansion header, on UART1
//   GPS TX  -> ESP32 GPIO 14  (GPS_RX_PIN)
//   GPS RX  -> ESP32 GPIO 21  (GPS_TX_PIN)
//   GPS VCC -> 5V (or 3.3V), GND -> GND
// ---------------------------------------------------------------------------
#define GPS_RX_PIN 14
#define GPS_TX_PIN 21
#define GPS_DEFAULT_BAUD 115200      // BE-252Q factory default
#define GPS_TARGET_RATE_HZ 10        // requested via UBX (ignored if the chip isn't u-blox)
#define GPS_CONFIGURE_UBLOX 1        // send UBX config (RAM only, never saved to the module)
#define GPS_ECHO_NMEA 0
#define GPS_AUTO_SIMULATOR 1         // 1 = no GPS found at boot -> run the simulator (shows SIM)              // 1 = copy raw NMEA to the USB serial monitor
#define GPS_MIN_SATELLITES 4
#define GPS_FIX_TIMEOUT_MS 3000      // no fresh fix for this long = fix lost

// ---------------------------------------------------------------------------
// Display (ILI9341V 240x320, used in landscape 320x240)
// ---------------------------------------------------------------------------
#define DISPLAY_ROTATION 1           // 1 or 3 for landscape
#define DISPLAY_INVERT 1             // IPS panel: if the background shows WHITE, set to 0
#define DISPLAY_FPS 10
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
// Storage
// ---------------------------------------------------------------------------
#define TRACK_DATABASE_FILE "/tracks.json"
#define SESSION_DIR "/sessions"

// ---------------------------------------------------------------------------
// Simulator (enabled by the es3c28p-sim build environment)
// ---------------------------------------------------------------------------
#ifndef SIMULATE_GPS
#define SIMULATE_GPS 0
#endif

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
