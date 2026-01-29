/**
 * Track Rat - Configuration for ESP32-S3 N16R8
 * Hardware: ESP32-S3 with built-in 2.8" TFT display and SD card
 * GPS: Beitian BE-252Q
 */

#ifndef CONFIG_H
#define CONFIG_H

// Hardware Configuration
#define ESP32_S3_N16R8

// GPS Configuration - Beitian BE-252Q
// High-performance GPS module with better sensitivity and faster updates
#define GPS_RX_PIN 44  // Adjust based on your board's pinout
#define GPS_TX_PIN 43  // Adjust based on your board's pinout
#define GPS_BAUD 38400  // BE-252Q default baud rate (can be 9600, 38400, 57600, 115200)
#define GPS_UPDATE_RATE 10  // Hz - BE-252Q supports up to 10Hz

// TFT Display Configuration (ST7789 240x320)
// Built-in display on ESP32-S3-28P
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_ROTATION 1  // 0=portrait, 1=landscape, 2=portrait inverted, 3=landscape inverted

// TFT pins (adjust based on your specific board)
#define TFT_CS   10
#define TFT_DC   11
#define TFT_RST  12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_BL   15  // Backlight pin

// SD Card Configuration
// Built-in SD card reader
#define SD_CS    21  // Adjust based on your board
#define SD_MOSI  23
#define SD_MISO  19
#define SD_SCK   18

// For boards with shared SPI
#define USE_SHARED_SPI true

// Button Configuration
// You may want to use touch screen buttons instead of physical buttons
#define BUTTON_SELECT_PIN 0   // Boot button can be used
#define BUTTON_NEXT_PIN 1     // Or use touch screen buttons
#define BUTTON_PREV_PIN 2     // Or use touch screen buttons
#define DEBOUNCE_DELAY 50     // milliseconds

// Touch Screen (if available on your display)
#define USE_TOUCH_SCREEN false  // Set to true if your display has touch
#define TOUCH_CS 33  // Adjust based on your board

// Track Detection
#define TRACK_DETECTION_RADIUS 5000.0  // meters - max distance to consider track as "nearby"
#define FINISH_LINE_DETECTION_RADIUS 15.0  // meters - crossing detection
#define MIN_LAP_TIME 30000  // milliseconds - minimum valid lap time (30 seconds)
#define MAX_LAP_TIME 600000  // milliseconds - maximum valid lap time (10 minutes)

// Storage Configuration
#define USE_SD_CARD true  // Use SD card for primary storage
#define TRACK_DATABASE_FILE "/tracks.json"
#define SESSION_DATA_DIR "/sessions"
#define MAX_LAPS_STORED 1000  // SD card can store many more laps

// Timing
#define DISPLAY_UPDATE_INTERVAL 50  // milliseconds - faster updates for TFT
#define GPS_TIMEOUT 5000  // milliseconds

// GPS Performance Settings for BE-252Q
#define GPS_MIN_SATELLITES 4  // Minimum satellites for valid fix
#define GPS_MAX_HDOP 2.5  // Maximum horizontal dilution of precision
#define GPS_MIN_SPEED_THRESHOLD 1.0  // km/h - minimum speed to consider as moving

// Display Colors (RGB565 format)
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_GRAY      0x8410
#define COLOR_DARKGRAY  0x4208
#define COLOR_ORANGE    0xFD20

// UI Colors
#define UI_BACKGROUND   COLOR_BLACK
#define UI_TEXT         COLOR_WHITE
#define UI_HEADER_BG    COLOR_BLUE
#define UI_HEADER_TEXT  COLOR_WHITE
#define UI_HIGHLIGHT    COLOR_GREEN
#define UI_WARNING      COLOR_YELLOW
#define UI_ERROR        COLOR_RED
#define UI_LAP_TIME     COLOR_CYAN

// Debug Configuration
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
