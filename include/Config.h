/**
 * Track Rat - Configuration
 * ESP32 GPS Lap Timer
 */

#ifndef CONFIG_H
#define CONFIG_H

// GPS Configuration
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define GPS_BAUD 9600
#define GPS_UPDATE_RATE 10  // Hz

// Display Configuration (I2C OLED SSD1306)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define I2C_SDA 21
#define I2C_SCL 22

// Track Detection
#define TRACK_DETECTION_RADIUS 5000.0  // meters - max distance to consider track as "nearby"
#define FINISH_LINE_DETECTION_RADIUS 15.0  // meters - crossing detection
#define MIN_LAP_TIME 30000  // milliseconds - minimum valid lap time
#define MAX_LAP_TIME 600000  // milliseconds - maximum valid lap time

// UI Configuration
#define BUTTON_SELECT_PIN 13  // Button to select track
#define BUTTON_NEXT_PIN 14    // Button to navigate tracks
#define BUTTON_PREV_PIN 12    // Button to navigate tracks
#define DEBOUNCE_DELAY 50     // milliseconds

// Storage
#define TRACK_DATABASE_FILE "/tracks.json"
#define SESSION_DATA_FILE "/sessions.json"
#define MAX_LAPS_STORED 100

// Timing
#define DISPLAY_UPDATE_INTERVAL 100  // milliseconds
#define GPS_TIMEOUT 5000  // milliseconds

// Debug
#define DEBUG_MODE true
#define DEBUG_SERIAL Serial

#if DEBUG_MODE
  #define DEBUG_PRINT(x) DEBUG_SERIAL.print(x)
  #define DEBUG_PRINTLN(x) DEBUG_SERIAL.println(x)
  #define DEBUG_PRINTF(x, ...) DEBUG_SERIAL.printf(x, __VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(x, ...)
#endif

#endif // CONFIG_H
