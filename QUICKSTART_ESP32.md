# Track Rat ESP32 - Quick Start Guide

Get up and running in 15 minutes!

## What You Need

### Hardware (Total: ~$25-35)
- ESP32 Dev Board ($5-8)
- u-blox NEO-6M GPS Module ($8-12)
- SSD1306 OLED Display 128x64 ($5-8)
- 3x Push Buttons ($1-2)
- Jumper Wires ($2-3)
- USB Cable ($2-3)

### Software
- Computer with USB port
- PlatformIO (or Arduino IDE)

## Quick Setup (15 Minutes)

### Step 1: Wiring (5 minutes)

```
GPS Module → ESP32
TX  → GPIO 16
RX  → GPIO 17
VCC → 5V
GND → GND

OLED Display → ESP32
SDA → GPIO 21
SCL → GPIO 22
VCC → 3.3V
GND → GND

Buttons → ESP32
Button 1 (SELECT) → GPIO 13 → GND
Button 2 (NEXT)   → GPIO 14 → GND
Button 3 (PREV)   → GPIO 12 → GND
```

### Step 2: Software Installation (5 minutes)

**Option A: PlatformIO (Recommended)**

```bash
# Install PlatformIO
pip install platformio

# Clone project
git clone https://github.com/yourusername/track-rat.git
cd track-rat

# Build and upload code
pio run --target upload

# Upload track database
pio run --target uploadfs

# Open serial monitor
pio device monitor
```

**Option B: Arduino IDE**

1. Install ESP32 board support in Arduino IDE
2. Install libraries: ArduinoJson, Adafruit_GFX, Adafruit_SSD1306, TinyGPSPlus
3. Open `src/main.cpp`
4. Upload code
5. Upload filesystem data

### Step 3: Configure Your Track (3 minutes)

Edit `data/tracks.json` - find the "custom-track" entry:

```json
{
  "id": "my-track",
  "name": "My Local Track",
  "location": "Your City",
  "country": "Your Country",
  "length": 2500.0,
  "startFinish": {
    "lat": 40.123456,    ← YOUR FINISH LINE LATITUDE
    "lon": -74.123456,   ← YOUR FINISH LINE LONGITUDE
    "radius": 15.0
  }
}
```

**Get coordinates:**
- Use Google Maps: Right-click → "What's here?"
- Or GPS app on your phone at the finish line

Re-upload filesystem:
```bash
pio run --target uploadfs
```

### Step 4: First Run (2 minutes)

1. Power on ESP32
2. Wait for boot screen
3. GPS acquires fix (30-60 seconds outdoors)
4. System auto-selects nearest track OR shows track selection
5. Press SELECT button to confirm
6. Drive to start/finish line to begin timing

## Usage Summary

### Button Controls

| Button | Function |
|--------|----------|
| SELECT | Confirm selection / Pause-Resume timer |
| NEXT | Next track (in selection mode) |
| PREV | Previous track (in selection mode) |

### Normal Operation Flow

1. **Boot** → Initializing...
2. **GPS Wait** → Acquiring satellites
3. **Track Selection** → Choose your track (or auto-detected)
4. **Confirmation** → Press SELECT to start
5. **Lap Timing** → Cross finish line to start lap
6. **Session Summary** → View your results

### What You'll See During Laps

```
Current lap time (large)
Lap number
Last lap time
Best lap time
Delta to best lap
Current speed
```

## Testing Without Going to Track

You can test the system indoors:

1. Power on and wait for GPS fix (may need to be near window)
2. Use NEXT/PREV to browse pre-loaded famous tracks
3. Press SELECT twice to enter lap timing mode
4. System will show 00:00.000 lap time (won't detect laps without movement)
5. Verify display and buttons work correctly

## Troubleshooting

### "No GPS Fix"
- Go outdoors with clear sky view
- Wait 1-2 minutes
- Check GPS wiring

### "Blank Display"
- Check I2C wiring (GPIO 21, 22)
- Verify 3.3V power to display
- Try I2C address 0x3D instead of 0x3C in Config.h

### "Buttons Don't Work"
- Check wiring to correct GPIO pins
- Buttons should connect GPIO to GND when pressed
- Internal pull-ups are enabled (no external resistors needed)

### "Track Not Detected"
- Make sure you're within 5km of a track in database
- Or use manual selection (NEXT/PREV buttons)
- Add your own track coordinates to database

## At The Track

### Preparation
1. Fully charge power bank or verify USB power
2. Mount securely in vehicle (away from airbags)
3. Ensure GPS has clear sky view
4. Start system before entering vehicle

### First Lap
1. System starts timing when you cross finish line
2. Complete full lap to register lap time
3. Each crossing records a new lap

### During Session
- Don't touch device while driving!
- System runs automatically
- GPS must maintain fix for accurate timing

### After Session
1. System shows session summary
2. Data automatically saved
3. Power off or start new session

## Next Steps

- Add your own track coordinates
- Experiment with split points for sector timing
- Review session data files
- Share your lap times!

## Tips for Best Results

1. **GPS Accuracy**
   - Let GPS acquire fix before driving
   - Mount with clear sky view
   - External antenna helps for in-car mounting

2. **Track Coordinates**
   - Use most accurate source available
   - Stand at finish line with phone GPS
   - Test detection radius (15m default)

3. **Power**
   - Use quality USB power source
   - Car USB adapter or power bank
   - Ensure stable power during session

4. **Mounting**
   - Secure mounting essential
   - Easy to see but not distracting
   - Don't obstruct controls or airbags

## Cost Breakdown

| Item | Price Range |
|------|-------------|
| ESP32 Board | $5-8 |
| GPS Module | $8-12 |
| OLED Display | $5-8 |
| Buttons (3x) | $1-2 |
| Wires & Misc | $2-5 |
| **Total** | **$21-35** |

Compare to commercial lap timers: $200-500!

## Pre-loaded Famous Tracks

Test the system at these tracks:
- Laguna Seca (USA)
- Nürburgring GP (Germany)
- Silverstone (UK)
- Circuit of the Americas (USA)
- Spa-Francorchamps (Belgium)
- Monza (Italy)
- Suzuka (Japan)

## Need Help?

- Check README_ESP32.md for detailed documentation
- GitHub Issues for bugs
- Serial monitor shows debug output

**Now go set some lap records! 🏁**
