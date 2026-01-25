# Track Rat - ESP32 GPS Lap Timer

**Offline GPS Lap Timer with Local Track Database**

A professional-grade GPS lap timer built for ESP32 that works completely offline with a local database of race track coordinates. Automatically detects your location and selects the nearest track, or lets you manually choose from a built-in track database.

## Features

### Core Functionality
- **Offline Operation**: All track data stored locally on ESP32
- **Auto Track Detection**: Automatically selects nearest track based on GPS location
- **Manual Track Selection**: Browse and select from stored tracks via GUI
- **Precise Lap Timing**: Uses GPS time or ESP32 timer for millisecond accuracy
- **Split Time Tracking**: Optional intermediate timing points
- **Session Storage**: Saves lap data to onboard flash storage

### User Interface
- **OLED Display**: Real-time lap times and track selection
- **Button Navigation**: Simple 3-button interface
- **Multiple Screens**: Boot, GPS wait, track selection, lap timing, session summary
- **Visual Feedback**: Progress bars, centered text, formatted times

### Track Database
- **Pre-loaded Tracks**: Includes famous circuits worldwide
- **Custom Tracks**: Easy to add your own local tracks
- **JSON Storage**: Simple format for editing and adding tracks
- **Coordinates**: Start/finish and optional split points for each track

## Hardware Requirements

### Required Components

| Component | Specification | Notes |
|-----------|--------------|-------|
| ESP32 Dev Board | Any ESP32 module | NodeMCU, DevKitC, etc. |
| GPS Module | u-blox NEO-6M/7M/8M | UART interface |
| OLED Display | SSD1306 128x64 I2C | 0.96" or 1.3" |
| Push Buttons | 3x momentary switches | For navigation |
| Power Supply | 5V USB or battery | 500mA+ recommended |

### Optional Components
- Enclosure/case
- External GPS antenna (better signal)
- Battery pack for portability
- MicroSD card (for extended data logging)

## Wiring Diagram

```
ESP32          GPS Module (NEO-6M)
GPIO 16   ->   TX
GPIO 17   ->   RX
GND       ->   GND
5V        ->   VCC

ESP32          OLED Display (SSD1306)
GPIO 21   ->   SDA
GPIO 22   ->   SCL
GND       ->   GND
3.3V      ->   VCC

ESP32          Buttons
GPIO 13   ->   SELECT Button -> GND
GPIO 14   ->   NEXT Button -> GND
GPIO 12   ->   PREV Button -> GND
```

## Software Installation

### Prerequisites

1. **PlatformIO** (recommended) or Arduino IDE
2. **USB Driver** for ESP32 (CP2102/CH340)
3. **Git** (for cloning repository)

### Installation Steps

#### Using PlatformIO (Recommended)

```bash
# Install PlatformIO Core
pip install platformio

# Clone repository
git clone https://github.com/yourusername/track-rat.git
cd track-rat

# Build and upload
pio run --target upload

# Upload filesystem (track database)
pio run --target uploadfs

# Monitor serial output
pio device monitor
```

#### Using Arduino IDE

1. Install ESP32 board support:
   - File → Preferences → Additional Board Manager URLs
   - Add: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → Search "ESP32" → Install

2. Install required libraries:
   - ArduinoJson (>= 6.21.3)
   - Adafruit GFX Library
   - Adafruit SSD1306
   - TinyGPSPlus

3. Open `src/main.cpp` in Arduino IDE
4. Select board: Tools → Board → ESP32 Dev Module
5. Upload code
6. Upload filesystem data (see below)

### Uploading Track Database

The track database must be uploaded to ESP32's LittleFS filesystem:

**PlatformIO:**
```bash
pio run --target uploadfs
```

**Arduino IDE:**
1. Install ESP32 Sketch Data Upload plugin
2. Tools → ESP32 Sketch Data Upload
3. Wait for upload to complete

## Configuration

### Adding Your Own Track

Edit `data/tracks.json` and add your track:

```json
{
  "id": "my-track",
  "name": "My Local Track",
  "location": "City Name",
  "country": "Country",
  "length": 2500.0,
  "startFinish": {
    "lat": 40.123456,
    "lon": -74.123456,
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

### Getting Track Coordinates

**Method 1: Google Maps**
1. Right-click on the start/finish line
2. Select "What's here?"
3. Copy the coordinates

**Method 2: GPS App**
1. Use a GPS app on your phone
2. Stand at the start/finish line
3. Note the latitude and longitude

**Method 3: Google Earth**
1. Navigate to the track
2. Click on the start/finish line
3. Coordinates shown at bottom of screen

### Configuration Options

Edit `include/Config.h` to customize:

```cpp
// GPS pins
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17

// Detection radius
#define FINISH_LINE_DETECTION_RADIUS 15.0  // meters

// Lap time validation
#define MIN_LAP_TIME 30000  // 30 seconds
#define MAX_LAP_TIME 600000  // 10 minutes

// Display update rate
#define DISPLAY_UPDATE_INTERVAL 100  // milliseconds
```

## Usage

### First Boot

1. Power on the ESP32
2. Display shows "TRACK RAT" boot screen
3. System initializes GPS and track database
4. Transitions to "GPS STATUS" screen

### Waiting for GPS Fix

1. Display shows satellite count
2. Progress bar indicates GPS signal strength
3. Wait for "FIX OK" status (typically 30-60 seconds outdoors)
4. Once GPS has fix, system moves to next step

### Track Selection

**Auto-Detection (If Near a Track):**
- System automatically finds nearest track within 5km
- Shows track confirmation screen
- Press SELECT to confirm and start

**Manual Selection (If No Nearby Track):**
- Display shows track selection screen
- Use NEXT/PREV buttons to browse tracks
- Press SELECT to confirm track
- System shows confirmation screen
- Press SELECT again to start

### Lap Timing

1. Drive to the start/finish line
2. System starts timing when you cross the line
3. Display shows:
   - Current lap time (large, top)
   - Lap number
   - Last lap time
   - Best lap time
   - Delta to best lap
   - Current speed

4. Each lap crossing creates a new lap record
5. Best lap is automatically tracked

### During Session

- **SELECT Button**: Pause/Resume lap timer
- Timer continues running until you pause or power off
- GPS fix must be maintained for accurate timing

### Session End

1. Press SELECT to pause
2. Session summary displayed:
   - Total laps completed
   - Best lap time
   - Best lap average speed
   - Session average lap time

3. Lap data automatically saved to flash storage
4. Press SELECT to return to track selection

## Display Screens

### Boot Screen
```
┌────────────────────┐
│    TRACK RAT       │
│  GPS Lap Timer     │
│  Initializing...   │
└────────────────────┘
```

### GPS Waiting
```
┌────────────────────┐
│   GPS STATUS       │
├────────────────────┤
│ Satellites: 8      │
│ Status: FIX OK     │
│                    │
│ [████████████] 100%│
└────────────────────┘
```

### Track Selection
```
┌────────────────────┐
│  SELECT TRACK      │
├────────────────────┤
│   Laguna Seca      │
│  Monterey, CA      │
│    3.60 km         │
│      <1/8>         │
└────────────────────┘
```

### Lap Timing
```
┌────────────────────┐
│   01:23.456        │
│ Lap: 5             │
│ Last: 01:22.123    │
│ Best: 01:21.987    │
│ D: +1.469          │
│ SPD: 125.3 km/h    │
└────────────────────┘
```

### Session Summary
```
┌────────────────────┐
│  SESSION END       │
├────────────────────┤
│ Laps: 12           │
│ Best: 01:21.987    │
│ @ 134.5 km/h       │
│ Avg: 01:23.456     │
└────────────────────┘
```

## Track Database

### Pre-loaded Tracks

The system comes with these famous race tracks:

1. **Laguna Seca** - Monterey, CA, USA (3.6 km)
2. **Nürburgring GP** - Nürburg, Germany (5.1 km)
3. **Silverstone** - Silverstone, UK (5.9 km)
4. **Circuit of the Americas** - Austin, TX, USA (5.5 km)
5. **Spa-Francorchamps** - Stavelot, Belgium (7.0 km)
6. **Monza** - Monza, Italy (5.8 km)
7. **Suzuka Circuit** - Suzuka, Japan (5.8 km)
8. **Custom Track** - Template for your track

### Track Database Structure

```json
{
  "tracks": [
    {
      "id": "unique-track-id",
      "name": "Track Name",
      "location": "City, State/Province",
      "country": "Country",
      "length": 5000.0,
      "startFinish": {
        "lat": 40.123456,
        "lon": -74.123456,
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
  ]
}
```

## Data Storage

### Session Data

Each session is automatically saved to flash storage:

**File Format:** `/session_<timestamp>.json`

**Contents:**
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

### Storage Management

- ESP32 has ~3MB flash for data
- Each session ~1-2KB depending on lap count
- Can store hundreds of sessions
- Manually delete old sessions via serial monitor or reflash filesystem

## Troubleshooting

### GPS Not Getting Fix

**Symptoms:** Stuck on "Waiting for GPS" screen

**Solutions:**
1. Go outdoors with clear sky view
2. Wait 1-2 minutes for cold start
3. Check GPS module wiring
4. Verify GPS module LED is blinking
5. Check serial monitor for GPS data:
   ```bash
   pio device monitor
   ```

### Display Not Working

**Symptoms:** Blank screen or garbled display

**Solutions:**
1. Check I2C wiring (SDA=21, SCL=22)
2. Verify display address (usually 0x3C):
   ```cpp
   // Try changing in Config.h
   #define SCREEN_ADDRESS 0x3C  // or 0x3D
   ```
3. Test with I2C scanner sketch
4. Check 3.3V power supply

### Buttons Not Responding

**Symptoms:** Cannot navigate menus

**Solutions:**
1. Check button wiring and pull-up resistors
2. Verify GPIO pins in Config.h
3. Test button continuity with multimeter
4. Check debounce timing

### Track Not Auto-Detecting

**Symptoms:** Always shows manual selection

**Solutions:**
1. Ensure you're within 5km of a track in database
2. Check track coordinates in tracks.json
3. Verify GPS has accurate fix
4. Increase detection radius in Config.h:
   ```cpp
   #define TRACK_DETECTION_RADIUS 10000.0  // 10km
   ```

### Lap Not Registering

**Symptoms:** Crossing finish line doesn't trigger lap

**Solutions:**
1. Verify you crossed within detection radius (default 15m)
2. Check GPS accuracy (need good fix)
3. Ensure lap time is within valid range (30s - 10min)
4. Increase finish line radius for your track
5. Check serial monitor for debug messages

## Advanced Features

### Debug Mode

Enable verbose serial output in `Config.h`:

```cpp
#define DEBUG_MODE true
```

Monitor via serial:
```bash
pio device monitor
# or
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

### Custom Lap Time Limits

Adjust for your track in `Config.h`:

```cpp
#define MIN_LAP_TIME 20000   // 20 seconds for karting
#define MAX_LAP_TIME 180000  // 3 minutes for beginners
```

### Adding More Split Points

Edit your track in `data/tracks.json`:

```json
"splits": [
  {"lat": 40.124, "lon": -74.124, "radius": 15.0},
  {"lat": 40.125, "lon": -74.125, "radius": 15.0},
  {"lat": 40.126, "lon": -74.126, "radius": 15.0}
]
```

## Power Options

### USB Power
- Connect to computer or USB power bank
- Simplest option for testing
- ~150-200mA current draw

### Battery Power
- Use 3.7V LiPo battery with voltage regulator
- Or 5V USB power bank
- Enables portable use
- 2000mAh battery = ~10 hours runtime

### Car Power
- Use 12V cigarette lighter USB adapter
- Quality adapter recommended (2A minimum)
- Protect from voltage spikes

## Safety Notice

⚠️ **IMPORTANT SAFETY INFORMATION**

- This device is for CLOSED CIRCUIT track use only
- Never use on public roads
- Do not interact with device while driving
- Mount securely to prevent distraction
- Ensure GPS antenna has clear sky view
- Device should not obstruct vehicle controls
- Follow all track safety regulations

## Contributing

Want to add your local track to the database? Contributions welcome!

1. Fork the repository
2. Add your track to `data/tracks.json`
3. Test coordinates are accurate
4. Submit pull request

## License

MIT License - See LICENSE file

## Credits

Built for motorsport enthusiasts who want professional lap timing without expensive hardware.

**Libraries Used:**
- TinyGPS++ by Mikal Hart
- ArduinoJson by Benoit Blanchon
- Adafruit GFX & SSD1306

## Support

- **Issues**: GitHub Issues
- **Discussions**: GitHub Discussions
- **Updates**: Watch the repository

## Version History

**v2.0.0 - ESP32 Offline Version**
- Complete rewrite for ESP32
- Offline track database
- Auto track detection
- Button-based UI
- Session data storage
- Multiple pre-loaded tracks

**v1.0.0 - Raspberry Pi Version**
- Initial release
- Python-based
- Single track configuration

---

**Happy Racing! 🏁**
