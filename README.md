# Track Rat

Offline GPS lap timer for the **LCDWiki ES3C28P** (ESP32-S3 N16R8 with a 2.8" touch display and SD slot) and a **Beitian BE-252Q** GPS.

- Works completely offline. Track start/finish coordinates live on the device.
- Picks the nearest track automatically (within 5 km), or you choose one on the touchscreen.
- Lap and split times come from GPS timestamps. The crossing is interpolated between fixes, so timing resolution is a few milliseconds even at 1 Hz.
- Each session is saved to the SD card (or internal flash) after every lap.

First time? Follow **[TESTING.md](TESTING.md)**. It walks through flashing and checking each part of the hardware.

## Hardware

| Part | Notes |
|---|---|
| ES3C28P board | ESP32-S3 N16R8, ILI9341V 240x320 IPS, FT6336G capacitive touch, SDIO microSD |
| Beitian BE-252Q | UART NMEA, factory default 115200 baud (auto-detected) |
| microSD card | Optional, FAT32. Holds `tracks.json` and session logs |

### Wiring (GPS to the board's expansion header)

| BE-252Q | ES3C28P |
|---|---|
| TX | **GPIO 14** |
| RX | **GPIO 21** |
| VCC | 5V |
| GND | GND |

The BE-252Q's compass wires (SDA/SCL) aren't used.

Everything else (display, touch, SD) is built into the board. The pins are set in `include/Config.h` and `platformio.ini`.

## Build and flash

Needs [PlatformIO](https://platformio.org/) (VS Code extension or `pip install platformio`).

```bash
pio run -e es3c28p -t upload        # lap timer
pio device monitor                  # serial log, 115200 baud
```

Other targets:

```bash
pio run -e es3c28p-sim -t upload    # desk simulator: fake car lapping a fake track, no GPS needed
pio run -e es3c28p -t uploadfs      # store data/tracks.json in internal flash
```

## Using it

| Screen | Touch | BOOT button |
|---|---|---|
| Waiting for GPS | tap: choose a track now | press: choose a track now |
| Select track | left / right third: previous / next, centre: select | press: next, hold 1 s: select |
| Ready | centre: start, left: back | hold 1 s: start, press: back |
| Timing | hold 2 s: end session | hold 1 s: end session |
| Summary | tap: back to track list | press: back to track list |

When the GPS gets a fix, the nearest track within 5 km is selected and the Ready screen opens. Timing starts at the first start/finish crossing. Everything before that is the out lap.

While timing, the screen shows:

- **Current lap**, in large digits.
- **Last and best lap.**
- **Last vs best:** the time difference between your last lap and your best.
- **Split delta:** for 5 s after each split, the gap to your best lap at that split.
- **Speed.**

When you complete a lap, its time shows full-screen for 4 s, in green if it's a new best.

## Tracks

Tracks are loaded from `/tracks.json`, searched in this order:

1. The SD card
2. Internal flash (`uploadfs`)
3. The built-in list

The easiest way to add your own track is to edit the file on the SD card.

```json
{
  "tracks": [
    {
      "id": "my-track",
      "name": "My Local Track",
      "location": "Town, State",
      "country": "USA",
      "length": 2400,
      "startFinish": { "lat": 40.123456, "lon": -74.123456, "radius": 15 },
      "splits": [
        { "lat": 40.124500, "lon": -74.120100, "radius": 15 }
      ]
    }
  ]
}
```

- **`startFinish` and `splits`:** a point on the racing line. A lap or split counts when your path passes through that point, within `radius` metres either side, travelling in any direction.
- **`radius`:** about half the track width plus a few metres. 10–20 m is typical.
- **Placement:** put splits where no other part of the track passes within `radius`.
- **`splits`:** optional.
- **Missing coordinates:** tracks whose coordinates are `0, 0` are skipped.

> **The pre-loaded track coordinates are approximate.** Check the start/finish point of any track you drive (satellite view in Google Maps works well) before trusting its lap times.

## Session logs

Each session is saved as `/sessions/<UTC date>_<time>_<track id>.json` on the SD card, or in flash if there's no card. The file holds:

- every lap time
- every split time
- max and average speed per lap
- the best lap

The file is rewritten after every lap.

## Configuration

Everything else is in `include/Config.h`. The settings you're most likely to change:

| Setting | Default | What it does |
|---|---|---|
| `GPS_RX_PIN` / `GPS_TX_PIN` | 14 / 21 | GPS UART pins |
| `GPS_TARGET_RATE_HZ` | 10 | Requested fix rate. Only works on a u-blox chip, otherwise the module's default applies |
| `GPS_ECHO_NMEA` | 0 | 1 = copy raw GPS output to the serial monitor |
| `DISPLAY_INVERT` | 1 | Set to 0 if colours look inverted (white background) |
| `TOUCH_SWAP_XY`, `TOUCH_INVERT_X/Y` | 1, 0, 1 | Fix touch orientation if taps land in the wrong place |
| `MIN_LAP_TIME_MS` | 20000 | Start/finish crossings sooner than this are ignored |
| `TRACK_DETECTION_RADIUS_M` | 5000 | Range for automatic track selection |

## Tests

The lap-timing logic has no Arduino dependencies and is tested on a PC:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_laptimer.cpp src/LapTimer.cpp -o /tmp/lt && /tmp/lt
```

It checks that the timer:

- measures known-length laps to within 2 ms at both 10 Hz and 1 Hz, including laps that run past UTC midnight
- times splits correctly
- tracks the best lap and computes deltas
- doesn't record false laps while parked on the line with GPS jitter
- ignores a line crossing that happens during a GPS dropout

## Layout

```
include/   Config.h, GeoMath.h (gate maths), headers
src/       main.cpp (state machine), GPSHandler, LapTimer, TrackDatabase,
           DisplayManager, InputManager, Storage, SessionLog
data/      tracks.json
test/      host-side lap timer tests
legacy/    the original Raspberry Pi / Python prototype
```
