# Track Rat - ESP32-S3 GPS Lap Timer

**Professional GPS Lap Timer for ESP32-S3 with Offline Track Database**

Designed for the **ESP32-S3 N16R8** with built-in 2.8" TFT display and SD card, using the **Beitian BE-252Q** GPS module for high-precision track timing.

![Track Rat](https://img.shields.io/badge/version-2.0.0-blue) ![Platform](https://img.shields.io/badge/platform-ESP32--S3-green) ![License](https://img.shields.io/badge/license-MIT-orange)

## 🏁 Features

### Hardware-Specific
- **ESP32-S3 N16R8**: 16MB Flash + 8MB PSRAM for extensive data storage
- **Built-in 2.8" TFT Display**: 240x320 ST7789 color display with vivid graphics
- **Built-in SD Card**: Store unlimited sessions and track data
- **Beitian BE-252Q GPS**: High-sensitivity GPS with 10Hz update rate
- **Low Power**: ~200mA typical operation

### Core Features
- ✅ **Fully Offline**: All track data stored locally, no internet required
- ✅ **Auto Track Detection**: Finds nearest track within 5km automatically
- ✅ **Manual Track Selection**: Browse pre-loaded world-famous tracks
- ✅ **Millisecond Precision**: GPS-synchronized timing
- ✅ **Split Times**: Sector timing for detailed lap analysis
- ✅ **Best Lap Tracking**: Real-time delta comparison
- ✅ **Session Storage**: All laps saved to SD card in JSON format
- ✅ **Colorful UI**: Professional TFT interface with multiple screens

### Pre-loaded Tracks
- Laguna Seca (USA)
- Nürburgring GP (Germany)
- Silverstone (UK)
- Circuit of the Americas (USA)
- Spa-Francorchamps (Belgium)
- Monza (Italy)
- Suzuka (Japan)
- + Easy to add your own!

## 📦 Hardware Requirements

### Main Components

| Component | Specification | Notes |
|-----------|--------------|-------|
| **ESP32-S3 Board** | ESP32-S3 N16R8 (C3-28P) | 16MB Flash, 8MB PSRAM, built-in display & SD |
| **GPS Module** | Beitian BE-252Q | High-sensitivity, 10Hz update, UART interface |
| **Power Supply** | 5V USB-C or battery | 500mA+ recommended |
| **Buttons** | 3x momentary switches | SELECT, NEXT, PREV (optional - can use Boot button) |

**Total Cost**: ~$40-50 for complete system

### Why This Hardware?

**ESP32-S3 N16R8 Advantages:**
- 16MB Flash = Store thousands of laps
- 8MB PSRAM = Handle large datasets smoothly
- Built-in 2.8" color display = No external wiring
- Built-in SD card = Unlimited storage expansion
- USB-C native = Easy programming and power
- 240MHz dual-core = Smooth real-time performance

**Beitian BE-252Q GPS Advantages:**
- High sensitivity (-165dBm tracking)
- 10Hz update rate = smoother positioning
- 38400 baud default = faster data transfer
- Better accuracy than NEO-6M
- Lower power consumption
- Built-in antenna + U.FL for external

## 🔌 Wiring

### Beitian BE-252Q GPS Connection

```
GPS Module (BE-252Q)  →  ESP32-S3
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
VCC (Red)             →  5V
GND (Black)           →  GND
TX (Green)            →  GPIO 44 (RX)
RX (White)            →  GPIO 43 (TX)
PPS (optional)        →  Not connected
```

### Button Connections (Optional)

```
Button      →  ESP32-S3   →  Function
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
SELECT      →  GPIO 0     →  Confirm selection
NEXT        →  GPIO 1     →  Next track
PREV        →  GPIO 2     →  Previous track

All buttons connect: GPIO → Button → GND
(Internal pull-ups enabled)
```

**Note**: GPIO 0 is usually the BOOT button on most ESP32-S3 boards, so you get one button for free!

### Display & SD Card

**No wiring needed!** Both are built into the ESP32-S3-28P board.

### Complete Pin Assignment

```
Function         | GPIO Pin | Notes
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
GPS RX           | 44       | Adjust in Config.h
GPS TX           | 43       | Adjust in Config.h
Button SELECT    | 0        | Built-in BOOT button
Button NEXT      | 1        | Optional
Button PREV      | 2        | Optional
TFT Display      | Built-in | No external wiring
SD Card          | Built-in | No external wiring
USB-C            | Built-in | Power + programming
```

## 💻 Software Installation

### Prerequisites

```bash
# Required
- PlatformIO (recommended) or Arduino IDE
- USB-C cable
- Python 3.7+ (for PlatformIO)

# Optional
- Git (for version control)
- VS Code (best IDE for PlatformIO)
```

### Quick Installation (5 minutes)

```bash
# 1. Install PlatformIO
pip install platformio

# 2. Clone repository
git clone https://github.com/yourusername/track-rat.git
cd track-rat

# 3. Build and upload firmware
pio run --target upload

# 4. Upload track database to SD card
# Copy data/tracks.json to SD card manually or:
pio run --target uploadfs

# 5. Monitor serial output
pio device monitor
```

### Arduino IDE Installation

1. **Install ESP32-S3 Support**:
   - File → Preferences → Additional Board Manager URLs
   - Add: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → "ESP32" → Install

2. **Install Libraries**:
   - ArduinoJson (≥6.21.3)
   - TFT_eSPI (≥2.5.31)
   - TinyGPSPlus (≥1.0.3)

3. **Configure TFT_eSPI**:
   - Copy `User_Setup.h` to `Arduino/libraries/TFT_eSPI/`
   - Or edit `User_Setup_Select.h` to include ESP32-S3 settings

4. **Select Board**:
   - Tools → Board → ESP32 Arduino → ESP32-S3 Dev Module
   - Tools → PSRAM → "OPI PSRAM"
   - Tools → Flash Size → "16MB"
   - Tools → Partition Scheme → "Default 16MB with SPIFFS"

5. **Upload**:
   - Sketch → Upload
   - Copy `data/tracks.json` to SD card

## ⚙️ Configuration

### Adjust GPIO Pins

If your ESP32-S3 board has different pin assignments, edit `include/Config.h`:

```cpp
// GPS pins
#define GPS_RX_PIN 44  // Your board's GPS RX pin
#define GPS_TX_PIN 43  // Your board's GPS TX pin

// Button pins
#define BUTTON_SELECT_PIN 0   // Your SELECT button
#define BUTTON_NEXT_PIN 1     // Your NEXT button
#define BUTTON_PREV_PIN 2     // Your PREV button
```

### GPS Module Settings

The Beitian BE-252Q supports multiple baud rates:

```cpp
// In Config.h
#define GPS_BAUD 38400  // Options: 9600, 38400, 57600, 115200
#define GPS_UPDATE_RATE 10  // Hz - BE-252Q supports up to 10Hz
```

### Add Your Own Track

1. **Get Coordinates**:
   - Use Google Maps: Right-click → "What's here?"
   - Or GPS app on phone at the finish line

2. **Edit SD Card** `tracks.json`:
   ```json
   {
     "id": "my-local-track",
     "name": "My Home Track",
     "location": "Your City",
     "country": "Your Country",
     "length": 2500.0,
     "startFinish": {
       "lat": 40.123456,  ← YOUR LATITUDE
       "lon": -74.123456,  ← YOUR LONGITUDE
       "radius": 15.0
     },
     "splits": [
       {
         "lat": 40.124000,
         "lon": -74.124000,
         "radius": 15.0
       }
     ]
   }
   ```

3. **Save and restart** the device

### Display Customization

Adjust colors and UI in `Config.h`:

```cpp
// Display Colors (RGB565)
#define UI_BACKGROUND   COLOR_BLACK
#define UI_HEADER_BG    COLOR_BLUE
#define UI_HIGHLIGHT    COLOR_GREEN
#define UI_LAP_TIME     COLOR_CYAN
```

## 🚀 Usage

### First Power-On

1. **Boot Sequence**:
   - ESP32-S3 boots (2-3 seconds)
   - Display shows "TRACK RAT" logo
   - Initializes GPS and track database

2. **GPS Acquisition**:
   - Display shows "GPS STATUS"
   - Satellite count increases
   - Wait for "FIX OK" (30-60 seconds outdoors)
   - Progress bar shows signal strength

3. **Track Selection**:
   - **Auto-detection**: If within 5km of a track, automatically selected
   - **Manual**: Use NEXT/PREV buttons to browse tracks
   - Press SELECT to confirm

4. **Ready to Race**:
   - Drive to start/finish line
   - System starts timing on first crossing
   - Each lap automatically recorded

### Button Controls

| Button | Function |
|--------|----------|
| **SELECT** | Confirm selection / Pause timer |
| **NEXT** | Next track (in selection mode) |
| **PREV** | Previous track (in selection mode) |

### Display Screens

#### 1. Boot Screen
Shows Track Rat logo and initialization status

#### 2. GPS Wait Screen
- Satellite count
- Fix status
- Signal strength bars
- Progress bar

#### 3. Track Selection
- Track name (large)
- Location and country
- Track length
- Navigation (< 1/8 >)

#### 4. Track Confirmation
- Selected track details
- Distance to track
- "Press SELECT to Start"

#### 5. Lap Timing (Main Screen)
```
┌───────────────────────────┐
│     01:23.456             │ ← Current lap (huge)
│ Lap: 5                    │ ← Lap number
│ Last: 01:22.123           │ ← Last lap time
│ Best: 01:21.987           │ ← Best lap (green)
│ Delta: +1.469             │ ← Delta (red/green)
│ Speed: 125.3 km/h         │ ← Current speed
└───────────────────────────┘
```

#### 6. Session Summary
- Total laps completed
- Best lap time
- Best lap speed
- Average lap time
- "Session saved to SD card"

### Data Storage

All sessions automatically saved to SD card:

**File Location**: `/sessions/session_<timestamp>.json`

**Format**:
```json
{
  "laps": [
    {
      "lapNumber": 1,
      "lapTime": 85432,
      "timestamp": 43200000,
      "maxSpeed": 134.5,
      "avgSpeed": 112.3,
      "splitTimes": [25123, 48765]
    }
  ]
}
```

**Storage Capacity**: 16GB SD card = ~50,000+ sessions

## 🔧 Troubleshooting

### GPS Not Getting Fix

**Symptoms**: Stuck on "Waiting for GPS"

**Solutions**:
1. Go outdoors with clear sky view
2. Wait 60-90 seconds for cold start
3. Check GPS module LED is blinking
4. Verify wiring (TX→RX, RX→TX crossover)
5. Try different baud rate in Config.h
6. Check serial monitor for GPS data:
   ```bash
   pio device monitor
   # Should see NMEA sentences: $GPGGA, $GPRMC, etc.
   ```

### Display Issues

**Blank Screen**:
- Check TFT backlight pin connection
- Verify `User_Setup.h` is configured
- Try different TFT rotation in Config.h
- Test with TFT_eSPI example sketches

**Garbled Display**:
- Wrong pin configuration in `User_Setup.h`
- SPI frequency too high (lower to 20MHz)
- Bad connections on display

**Colors Wrong**:
- Check `ST7789_DRIVER` is defined
- Try `ST7789_2` driver variant
- Adjust color mode in User_Setup.h

### SD Card Not Detected

**Symptoms**: "SD card mount failed"

**Solutions**:
1. Format SD card as FAT32
2. Use quality SD card (Class 10+ recommended)
3. Check SD_CS pin in Config.h
4. Try lower SPI frequency
5. Ensure SD card is properly inserted
6. Test with SD card example sketch

### Buttons Not Working

**Symptoms**: Cannot navigate menus

**Solutions**:
1. Check button wiring (GPIO → Switch → GND)
2. Verify pull-ups enabled (INPUT_PULLUP)
3. Test continuity with multimeter
4. Check debounce timing
5. Use serial monitor to debug button presses

### Track Not Auto-Detecting

**Symptoms**: Always shows manual selection

**Solutions**:
1. Ensure within 5km of track in database
2. Check track coordinates are correct
3. Verify GPS has accurate fix
4. Increase detection radius:
   ```cpp
   #define TRACK_DETECTION_RADIUS 10000.0  // 10km
   ```

### Lap Not Registering

**Symptoms**: Crossing finish line doesn't trigger lap

**Solutions**:
1. GPS must have good fix (4+ satellites)
2. Check finish line radius (default 15m)
3. Ensure lap time within valid range (30s-10min)
4. Verify correct track coordinates
5. Increase finish line radius for testing:
   ```cpp
   #define FINISH_LINE_DETECTION_RADIUS 25.0  // 25 meters
   ```
6. Check serial monitor for debug messages

### Beitian BE-252Q Specific

**No GPS Data**:
1. Check baud rate matches (default 38400)
2. Verify TX/RX not swapped
3. Try external antenna if available
4. Check power (needs 3.3-5V, typically 5V)

**Slow Fix Acquisition**:
1. BE-252Q supports A-GPS - may need first outdoor use
2. Wait longer for almanac download (~12 minutes first time)
3. Subsequent fixes should be <30 seconds

## 🎯 Tips for Best Results

### GPS Accuracy
- Wait for 6+ satellites before racing
- Mount GPS with clear sky view (no metal blocking)
- External antenna improves in-car performance
- Beitian BE-252Q has better sensitivity than NEO-6M

### Track Configuration
- Walk finish line with GPS app for exact coordinates
- Test detection radius before race day
- Adjust radius based on track width (10-25m typical)
- Add split points at key corners for sector analysis

### Power Management
- Use quality 5V 2A power supply
- USB-C power bank recommended for portability
- Car USB adapters work but may have voltage fluctuations
- Monitor temperature in hot environments

### Data Analysis
- Copy SD card to computer for detailed analysis
- JSON format easy to import into Excel/Python
- Can export to popular lap timing software
- Backup sessions regularly

## 📊 Performance Specifications

| Specification | Value |
|--------------|-------|
| GPS Update Rate | 10 Hz (100ms) |
| Timing Precision | ±10ms |
| Position Accuracy | ~2.5m CEP (BE-252Q) |
| Display Refresh | 20 Hz (50ms) |
| Storage Capacity | 16MB Flash + SD Card |
| Session Storage | Thousands of sessions |
| Laps per Session | 1000+ supported |
| Boot Time | 2-3 seconds |
| GPS Cold Start | 30-90 seconds |
| GPS Hot Start | <5 seconds |
| Power Consumption | 200mA typical |
| Operating Temp | -20°C to +70°C |

## 📁 Project Structure

```
track-rat/
├── include/                  # Header files
│   ├── Config.h             # Main configuration
│   ├── TrackDatabase.h      # Track database
│   ├── GPSHandler.h         # GPS interface
│   ├── LapTimer.h           # Lap timing logic
│   └── DisplayManager.h     # TFT display
├── src/                     # Source files
│   ├── main.cpp             # Main application
│   ├── TrackDatabase.cpp    # Track management
│   ├── GPSHandler.cpp       # GPS processing
│   ├── LapTimer.cpp         # Timing logic
│   └── DisplayManager.cpp   # Display & UI
├── data/                    # Filesystem data
│   └── tracks.json          # Track database
├── platformio.ini           # PlatformIO config
├── User_Setup.h             # TFT_eSPI config
└── README_ESP32-S3.md       # This file
```

## 🔄 Updating Firmware

```bash
# Pull latest changes
git pull origin main

# Build and upload
pio run --target upload

# If track database updated, copy to SD card
# Or reflash filesystem:
pio run --target uploadfs
```

## 🤝 Contributing

Want to add tracks or features?

1. Fork the repository
2. Create feature branch
3. Add your track to `data/tracks.json`
4. Test thoroughly
5. Submit pull request

## 📝 License

MIT License - Free for personal and commercial use

## 🙏 Credits

**Hardware**:
- ESP32-S3 by Espressif Systems
- Beitian BE-252Q GPS module

**Software Libraries**:
- TFT_eSPI by Bodmer
- TinyGPS++ by Mikal Hart
- ArduinoJson by Benoit Blanchon

**Track Data**:
- Coordinates from public sources
- Community contributions welcome

## 📞 Support

- **Issues**: GitHub Issues
- **Discussions**: GitHub Discussions
- **Wiki**: Project Wiki
- **Email**: support@trackrat.dev

## 🎉 Version History

**v2.0.0** - ESP32-S3 Edition
- Complete rewrite for ESP32-S3
- TFT display with color UI
- SD card data storage
- Beitian BE-252Q GPS support
- Offline track database
- Auto track detection

**v1.0.0** - Raspberry Pi Edition
- Initial release
- OLED display
- Single track configuration

---

**Built with ❤️ for motorsport enthusiasts**

**Now get out there and set some lap records! 🏁**
