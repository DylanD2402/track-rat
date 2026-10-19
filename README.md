# Track Rat

Offline GPS lap timer for the **LCDWiki ES3C28P** (ESP32-S3 N16R8 with a 2.8" touch display and SD slot) and a **Beitian BE-252Q** GPS.

- Works completely offline. Track start/finish coordinates live on the device.
- Picks the nearest track automatically (within 5 km), or you choose one on the touchscreen.
- Lap and split times come from GPS timestamps. The crossing is interpolated between fixes, so timing resolution is a few milliseconds even at 1 Hz.
- Each session is saved to the SD card (or internal flash) after every lap.
- A Wi-Fi page on your phone downloads sessions, swaps the track list and installs firmware. No app needed.

First time? Follow **[TESTING.md](TESTING.md)**. It walks through flashing and checking each part of the hardware.

## Hardware

| Part | Notes |
|---|---|
| ES3C28P board | ESP32-S3 N16R8, ILI9341V 240x320 IPS, FT6336G capacitive touch, SDIO microSD |
| Beitian BE-252Q | UART NMEA, factory default 115200 baud (auto-detected) |
| microSD card | Optional, FAT32. Holds `tracks.json` and session logs |

### Wiring

Plug the BE-252Q into the board's **UART** connector (GPIO 43/44): GPS TX and RX to the connector's TX/RX, plus 5V and GND. The expansion header (GPIO 14/21) also works.

At boot the firmware tries both places and both orientations, so it doesn't matter if TX and RX are crossed. The GPS screen shows which GPIO it found the GPS on.

The BE-252Q's compass wires (SDA/SCL) aren't used.

Everything else (display, touch, SD) is built into the board. The pins are set in `include/Config.h` and `platformio.ini`.

## Build and flash

Needs [PlatformIO](https://platformio.org/) (VS Code extension or `pip install platformio`).

```bash
pio run -e es3c28p -t upload        # lap timer
python tools/serial_log.py --port COM4 --seconds 20 --reset   # restart and capture the log
```

To copy `data/` into internal flash (the Wi-Fi page, and `tracks.json` for boards without an SD card):

```bash
pio run -e es3c28p -t uploadfs
```

Do this once, and again when the Wi-Fi page changes. It replaces everything in internal flash, including sessions and tracks saved there when there was no SD card.

### Automatic flashing

`tools/autoflash.py` watches GitHub and puts every new commit on the board by itself:

```bash
python tools/autoflash.py --port COM4
```

Leave it running with the board plugged in. Every 30 s it checks the current branch for new commits. When there's one, it:

1. pulls the commit
2. builds and flashes the board
3. restarts the board and saves 30 s of its log to `logs/`

A failed build is reported and nothing is flashed. If you have uncommitted edits to the code, it won't pull until you commit or stash them.

- **`--now`:** also flash the current commit at startup.
- **`--push-logs`:** commits each log to a `device-logs` branch, so whoever made the change can read the result without you copying it. **The logs contain GPS positions, including where the board is when you're testing at home, so only use this if the repository is private.**

### Bench test

The last entry in the track list is **Bench Test**. Pick it and press START to run a simulated car around a small circuit, with real lap timing, splits, live diff and map, all at your desk. No GPS is needed, and if one is connected it's ignored for that session.

While a bench test runs, a yellow **BENCH** tag shows in the header. When the session ends, the real GPS is used again.

The board resets itself before and after uploading, so no buttons are needed. Only if an upload fails with "Failed to connect": hold BOOT, tap RST, release BOOT, and retry.

`tools/serial_log.py` reads the log without resetting the board. Most serial tools reset it into download mode when they open the port, which leaves it stuck until RST is pressed. `--reset` restarts the board normally first, so the log covers startup. It also works where `pio device monitor` can't, such as an AI assistant's terminal.

## Using it

**At a track:** after the logo, once the GPS has a fix, the nearest track within 5 km is picked automatically and the session starts at once. Timing begins the first time you cross the start/finish line; everything before that is the out lap.

**Away from a track:** you get a menu:

- **FREE ROAM:** records your drive. A map of your route draws as you go, with elapsed time, live speed, distance and top speed. Hold the screen to finish; the summary (and the route) is saved to the SD card.
- **NEW TRACK:** creates a track where you are. Drive to the start/finish line and **tap the screen as you cross it**. That sets the line and starts lap 1. After your first full lap, the lap is saved as the track's map. The track is saved to `tracks.json`, so next time it's picked automatically.
- **TRACK LIST:** pick any track by hand, including **Bench Test**.
- **WI-FI:** turns on the Wi-Fi page (see [Wi-Fi page](#wi-fi-page)).

**Tracks with no start/finish line yet** (from the Canada list, or created on the timer) ask you to tap the screen as you cross the line the first time. **SET LINE** on a track's Ready screen marks a new line for any track, for example if a stored line is in the wrong place.

| Screen | Touch | BOOT button |
|---|---|---|
| Waiting for GPS | tap: menu | press: menu |
| Menu | tap a mode or WI-FI; top bar: GPS status | press: next, hold 1 s: choose |
| Track list | left / right third: previous / next, centre: select, hold: menu | press: next, hold 1 s: select |
| GPS Status (top bar of the menu or track list) | tap: back | press: back |
| Ready | centre: start, left: back, right: set line | hold 1 s: start, press: back |
| Timing | tap: timing / map view, hold 2 s: end | press: switch view, hold 1 s: end |
| Setting the line | tap as you cross the line, hold 2 s: end | hold 1 s: end |
| Free Roam | hold 2 s: end | hold 1 s: end |
| Summaries | tap: continue; WI-FI button: Wi-Fi page | press: continue, hold 1 s: Wi-Fi page |
| Wi-Fi | tap: Wi-Fi off | press: Wi-Fi off |

While timing, the screen shows:

- **Current lap**, in large digits.
- **Last and best lap.**
- **Live diff:** how far ahead (green, `-`) or behind (red, `+`) your best lap you are right now, at the same point on track. It appears once you have a best lap.
- **Split time:** for 5 s after each split, the split time and its gap to your best lap's split.
- **GPS signal bars** in the top right: green with a fix, yellow while searching, red outline with no data.

When you complete a lap, its time shows full-screen for 4 s, in green if it's a new best.

The live diff compares your position with the path your best lap took. If you're more than 40 m off that path (`LIVE_DELTA_MAX_OFFSET_M`), for example in the pit lane, it shows `--`.

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
- **No start/finish yet:** a track can have a `"center": {"lat": .., "lon": ..}` instead of `startFinish`. It's still found automatically, and the timer asks you to tap at the line the first time.
- **Saved by the timer:** new tracks, start/finish lines marked on track, and maps from your first lap are written back into `tracks.json` on the SD card.

### All Canadian tracks

`tools/fetch_canada_tracks.py` adds every motorsport track in Canada from OpenStreetMap, each with its name, location and layout map. Run it on your PC; it needs Python 3, no extra packages.

```bash
python tools/fetch_canada_tracks.py
```

Then copy `data/tracks.json` to the SD card.

- **No start/finish lines:** OpenStreetMap doesn't record where the line is, so the timer asks you to mark it on your first visit.
- **What's skipped:** dirt tracks, motocross and drag strips, unless you add `--include-dirt` or `--include-drag`. Karting tracks are included; `--no-karting` leaves them out.
- **Your existing tracks are never changed.** That includes any track within 1 km of one you already have.

> **The pre-loaded track coordinates are approximate.** Check the start/finish point of any track you drive (satellite view in Google Maps works well) before trusting its lap times.

## Start-up logo

At power-on, the logo fades in behind a rippling edge that sweeps left to right, holds, then fades out the same way. This takes about 2.7 s, and it plays while the board mounts storage and finds the GPS, so it adds little or no time to startup. Press BOOT to skip it.

To use your own logo:

```bash
pip install pillow
python tools/png_to_logo.py my_logo.png
```

This writes `include/logo_image.h`. Rebuild and flash to see the new logo.

- **Size:** around 240–300 px wide and 80–140 px tall works best. Anything larger than the 320×240 screen is scaled down.
- **Background:** transparent areas become black.
- **Back to the placeholder:** delete `include/logo_image.h`.
- **Timing:** `SPLASH_FADE_MS` and `SPLASH_HOLD_MS` in `include/Config.h`.
- **Wave shape:** `BAND`, `AMP` and `WAVELEN` in `include/SplashSweep.h`.
- **Turning it off:** set `SPLASH_ENABLED` to `0` in `include/Config.h`.

## Track maps

While timing, tap the screen (or press BOOT) to switch between the big clock and a **map view**. The map view shows the track layout with your car as a white dot, plus the lap time, live diff, best and last lap down the right-hand side.

- **Tracks with a `"map"` in `tracks.json`:** the stored layout is drawn.
- **Tracks without one:** the map draws itself from your GPS path. It fills in during your first lap and then shows your best lap's line.

`tools/fetch_track_map.py` adds a track's real layout from OpenStreetMap. Run it on your PC; it needs Python 3, no extra packages. For example, Calabogie:

```bash
python tools/fetch_track_map.py --id calabogie --name "Calabogie Motorsports Park" \
    --location "Calabogie, ON" --country Canada --length 5050 --sf LAT,LON
```

- **`--sf`:** the start/finish line. In Google Maps satellite view, right-click the start/finish line and click the coordinates to copy them.
- **What it does:** downloads every `highway=raceway` way within 2.5 km, simplifies it, prints a text preview of the shape, and adds or updates the entry in `data/tracks.json`. Copy that file to the SD card.
- **Pit lane:** add `--no-pit` to leave it out.
- **Your own layout:** `--file layout.geojson` (or `.gpx` / `.kml`) uses a file you've drawn or exported instead.

## Phone apps (Bluetooth)

Track Rat sends its GPS over Bluetooth in the same format as a **RaceBox Mini**, so apps that support that device can use it as their GPS. The board's own screen and lap timing keep working at the same time.

- **Apps with RaceBox Mini support:** RaceChrono and RaceChrono Pro, Solostorm, NMEAconnect, AutoX DL and the RaceBox app. This list comes from the [RaceBox Mini emulator project](https://github.com/anchit92/Open-Source-RaceBox-mini-Emulator) that inspired this feature; I've not tested each app.
- **Connecting:** in the app, add a RaceBox Mini device. The board shows up as `RaceBox Mini` followed by 10 digits (the log prints the exact name at boot). While a phone is connected, a blue **BT** tag shows in the header.
- **Update rate:** one packet per GPS fix, so up to 10 a second.
- **Accelerometer:** this board has none, so the motion values read as a device lying still. Apps still calculate G-forces from the GPS.
- **Accuracy figures:** the accuracy fields are estimates from the satellite precision value (HDOP), because the GPS sends NMEA text rather than u-blox binary data.
- **Bench Test:** a bench session streams the simulated car, which is handy for setting up an app at a desk.
- **Turning it off:** set `BLE_ENABLED` to `0` in `include/Config.h`.

Track Rat isn't affiliated with RaceBox. The device name is used only so apps recognise the data format.

## Wi-Fi page

Get your sessions onto your phone without taking the SD card out.

1. In the pits, tap **WI-FI** on the menu or on a session summary. The screen shows the network name and address.
2. On your phone, join the Wi-Fi network **TrackRat-XXXX**. There's no password. The last 4 characters are different on each board.
3. The page opens by itself, like a hotel Wi-Fi login. If it doesn't, open **http://192.168.4.1** in your browser.
4. When you're done, tap the Track Rat screen (or press BOOT) to turn Wi-Fi off.

What the page does:

- **Sessions:** every file in `/sessions`, by day and track, newest first. Tap a file to download it. Files from the same session (`.json`, `.vbo` and so on) are shown together. Times are in your phone's time zone.
- **Tracks:** download the `tracks.json` Track Rat is using, or upload an edited one. The old one is kept as `tracks.json.bak`. Restart Track Rat to use the new list.
- **Firmware:** install a new `firmware.bin` (from `.pio/build/es3c28p/` after `pio run`). A progress bar shows while it's sent; then Track Rat restarts with Wi-Fi off. Keep it powered until it does.
- **Device:** firmware version, free space on the card and in flash, GPS status and memory.
- **Settings:** not there yet.

Each time the page opens it tells Track Rat your phone's time zone, which is kept for when file names use local time.

Good to know:

- **Wi-Fi is off while timing.** It's only on while the Wi-Fi screen shows, and starting a session turns it off. Bluetooth (RaceBox) keeps working while it's on.
- **Downloads in the sign-in window:** the window phones open by themselves often can't save files. If a download doesn't start, open http://192.168.4.1 in Safari or Chrome while still on the TrackRat network.
- **Mobile data:** some phones keep using mobile data for everything when a Wi-Fi network has no internet. If the page won't load, tell the phone to stay connected or turn mobile data off.
- **Anyone nearby can join** while it's on, so turn it off when you're done.

### Changing the page

The page is a single file, `web/index.html` (plain HTML and JavaScript, no libraries). `tools/build_web.py` gzips it into `data/web/index.html.gz` every time PlatformIO runs; it also puts the git version on the Device tab. To put a new page on the board:

```bash
pio run -e es3c28p -t uploadfs
```

Without it, the board shows a plain page with the session list, `tracks.json` and firmware upload.

## Session logs

Each session is saved as `/sessions/<UTC date>_<time>_<track id>.json` on the SD card, or in flash if there's no card. The file holds:

- every lap time
- every split time
- max and average speed per lap
- the best lap

The file is rewritten after every lap.

Free Roam drives are saved as `/sessions/<date>_<time>_freeroam.json`, with the duration, distance, top and average speed, and the route as GPS points about 10 m apart.

## Configuration

Everything else is in `include/Config.h`. The settings you're most likely to change:

| Setting | Default | What it does |
|---|---|---|
| `GPS_PIN_PAIRS` | 44/43, 43/44, 14/21, 21/14 | Pins searched for the GPS at boot |
| `GPS_TARGET_RATE_HZ` | 20 | Requested update rate (u-blox only). The GPS Status screen shows the rate actually reached |
| `GPS_USE_GALILEO` / `_GLONASS` / `_BEIDOU` | 1 / 0 / 0 | Satellite systems besides GPS. More systems = more satellites but a lower top rate (4 systems: 10 Hz, 2: 20 Hz) |
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
- computes the live diff to within about 40 ms of the true value

`test/test_racebox.cpp` checks the Bluetooth packet's fields, checksum and GPS time-of-week:

```bash
g++ -std=c++17 -Iinclude test/test_racebox.cpp src/RaceBoxPacket.cpp -o /tmp/rb && /tmp/rb
```
- doesn't record false laps while parked on the line with GPS jitter
- ignores a line crossing that happens during a GPS dropout

`test/test_freeroam.cpp` checks Free Roam's distance, top and average speed, that GPS jitter and position glitches add no distance, and that very long drives are capped:

```bash
g++ -std=c++17 -Iinclude test/test_freeroam.cpp -o /tmp/fr && /tmp/fr
```

## Layout

```
include/   Config.h, GeoMath.h (gate maths), headers
src/       main.cpp (state machine), GPSHandler, LapTimer, TrackDatabase,
           DisplayManager, InputManager, Storage, SessionLog, WifiPortal
web/       the Wi-Fi page (gzipped into data/web when building)
data/      tracks.json
test/      host-side lap timer tests
legacy/    the original Raspberry Pi / Python prototype
```
