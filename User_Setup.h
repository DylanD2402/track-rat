// TFT_eSPI User Setup for ESP32-S3 with ST7789 Display
// Copy this file to your Arduino/libraries/TFT_eSPI folder
// Or place in the root of your project

#define USER_SETUP_INFO "ESP32_S3_ST7789"

// Driver
#define ST7789_DRIVER

// Display size
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Pins - adjust these based on your actual board pinout
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   10
#define TFT_DC   11
#define TFT_RST  12
#define TFT_BL   15  // Backlight control

// Fonts
#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:.
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts

#define SMOOTH_FONT

// SPI frequency
#define SPI_FREQUENCY  40000000  // 40MHz
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

// Optional: Use DMA
#define DMA_CHANNEL 1
