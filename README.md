# Track Rat

Offline GPS lap timer for the **LCDWiki ES3C28P** (ESP32-S3 N16R8 with a 2.8" touch display and SD slot) and a **Beitian BE-252Q** GPS.

- Works completely offline. Track start/finish coordinates live on the device.
- Picks the nearest track automatically (within 5 km), or you choose one on the touchscreen.
- Lap and split times come from GPS timestamps. The crossing is interpolated between fixes, so timing resolution is a few milliseconds even at 1 Hz.
- Every GPS fix is logged to the SD card in Racelogic's VBO format, which the free Circuit Tools opens. Lap times are saved after every lap, with or without a card.
- Each session is saved to the SD card (or internal flash) after every lap.
- Optional battery with a percentage gauge; sleeps by itself when parked. Firmware can be updated from the SD card.
- Acceleration and braking runs (0-100 km/h, 1/4 mile, 100-0 braking) with a result card for each run.
- A Wi-Fi page on your phone downloads sessions, swaps the track list and installs firmware. No app needed.
- Speed units, screen direction, brightness and more are set on the device or on the Wi-Fi page and kept in flash: see [Settings](#settings).

First time? Follow **[TESTING.md](TESTING.md)**. It walks through flashing and checking each part of the hardware.

## Hardware

| Part | Notes |
|---|---|
| ES3C28P board | ESP32-S3 N16R8, ILI9341V 240x320 IPS, FT6336G capacitive touch, SDIO microSD |
| Beitian BE-252Q | u-blox M10 over UART, factory default 115200 baud (auto-detected) |
| microSD card | Optional, FAT32. Holds `tracks.json` and session logs |

### Wiring

Plug the BE-252Q into the board's **UART** connector (GPIO 43/44): GPS TX and RX to the connector's TX/RX, plus 5V and GND. The expansion header (GPIO 14/21) also works.

At boot the firmware tries both places and both orientations, so it doesn't matter if TX and RX are crossed. The GPS screen shows which GPIO it found the GPS on.

The BE-252Q's compass wires (SDA/SCL) aren't used.

### GPS data

The BE-252Q is built on a u-blox M10. At boot Track Rat asks it for 20 fixes a second (10 if the [GPS mode setting](#settings) is on all systems) as binary **UBX-NAV-PVT** messages, and once those arrive it turns the NMEA text sentences off. Each message carries the time to the millisecond, position, speed, vertical speed, and the GPS's own estimate of how accurate the position and speed are.

- **Other GPS modules:** if no UBX-NAV-PVT arrives within 3 seconds, Track Rat reads NMEA text instead (GGA and RMC) as before. There are no accuracy figures then, only HDOP.
- **Settings stay in the GPS's RAM:** nothing is saved to the module. If the GPS loses power and comes back sending NMEA, Track Rat asks for UBX-NAV-PVT again.
- **GPS Status screen:** the Data line says `UBX` or `NMEA`. With a u-blox GPS the Fix line shows position and speed accuracy, for example `LOCKED   ±1.2 m   ±0.5 km/h   3D`.

Everything else (display, touch, SD) is built into the board. The pins are set in `include/Config.h` and `platformio.ini`.

An optional big button and LED delta bar can go on IO21 and IO2: see [Big button and LED bar](#big-button-and-led-bar).

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

When a commit changes the Wi-Fi page (`web/`) or `data/` (such as `tracks.json`), it also runs `uploadfs`. **That wipes sessions and tracks saved in internal flash** (boards without an SD card); the SD card is never touched. `--uploadfs` does it on every commit.

- **`--now`:** also flash the current commit at startup.
- **`--push-logs`:** commits each log to a `device-logs` branch, so whoever made the change can read the result without you copying it. **The logs contain GPS positions, including where the board is when you're testing at home, so only use this if the repository is private.**

### Bench test

The last entry in the track list is **Bench Test**. Pick it and press START to run a simulated car around a small circuit, with real lap timing, splits, live diff and map, all at your desk. No GPS is needed, and if one is connected it's ignored for that session.

While a bench test runs, a yellow **BENCH** tag shows in the header. When the session ends, the real GPS is used again.

The board resets itself before and after uploading, so no buttons are needed. Only if an upload fails with "Failed to connect": hold BOOT, tap RST, release BOOT, and retry.

`tools/serial_log.py` reads the log without resetting the board. Most serial tools reset it into download mode when they open the port, which leaves it stuck until RST is pressed. `--reset` restarts the board normally first, so the log covers startup. It also works where `pio device monitor` can't, such as an AI assistant's terminal.

## Using it

**At a track:** after the logo, once the GPS has a fix, the nearest track within 5 km is picked automatically. If you're already moving (20 km/h or more), the session starts at once. If you're parked, you get that track's Ready screen, and the session starts by itself when you drive off (20 km/h for 2 seconds). Until then **MENU** (or pressing BOOT) takes you to the main menu, so a track made at home doesn't trap you in a session. Timing begins the first time you cross the start/finish line; everything before that is the out lap.

**Away from a track:** you get a menu:

- **FREE ROAM:** records your drive. A map of your route draws as you go, with elapsed time, live speed, distance and top speed. Hold the screen to finish; the summary (and the route) is saved to the SD card, unless **Record road drives** is off in [Settings](#settings). Then the drive is shown but not saved.
- **NEW TRACK:** creates a track where you are. Drive to the start/finish line and **tap the screen as you cross it**. That sets the line and starts lap 1. After your first full lap, the lap becomes the track's map, saved when you finish the session. The track is saved to `tracks.json`, so next time it's picked automatically.
- **TRACK LIST:** pick any track by hand. The nearest track comes first, then the rest in order of distance, each with how far away it is. Layouts of one venue stay together. Without a GPS fix the list is in `tracks.json` order, and it sorts itself as soon as there is a fix. **Bench Test** is always last.
- **PERFORMANCE:** the left button of the second row. Times acceleration and braking runs; see [Acceleration and braking](#acceleration-and-braking).
- **SETTINGS:** the middle button of the second row. Units, brightness and the rest; see [Settings](#settings).
- **WI-FI:** the right button of the second row. Turns on the Wi-Fi page (see [Wi-Fi page](#wi-fi-page)).

**Tracks with no start/finish line yet** (from the Canada list, or created on the timer) ask you to tap the screen as you cross the line the first time. **SET LINE** on a track's Ready screen marks a new line for any track, for example if a stored line is in the wrong place. The direction you're driving when you tap is saved with the line, so crossing it the other way later doesn't count. This needs you to be doing at least 15 km/h when you tap; slower than that the GPS direction is too rough, and the line is saved without one.

**Layouts:** a venue with several layouts (Full, Short, the kart track) lists each one separately, and its Ready screen shows which layout is picked. Tap the top bar of the Ready screen for the next layout. You don't have to get it right: after your first full lap, the timer compares your lap with the map of each layout there (and any other track within 3 km that has a map). If another one clearly fits better, it switches to it and shows **Layout: ...** on screen. If you haven't crossed the picked layout's line after 2 km, it checks the same way. It doesn't when you're putting a line down yourself (SET LINE, NEW TRACK): the line goes on the track you chose.

| Screen | Touch | BOOT button (or big button) |
|---|---|---|
| Waiting for GPS | tap: menu | press: menu |
| Menu | tap a mode, or PERFORMANCE / SETTINGS / WI-FI in the second row; top bar: GPS status | press: next (in the order on screen), hold 1 s: choose |
| Track list | left / right third: previous / next, centre: select, top bar or hold: menu | press: next, hold 1 s: select |
| GPS Status (top bar of the menu) | tap: back | press: back |
| Ready | centre: start, left: menu (back, if you came from the track list), right: set line, top bar: next layout | hold 1 s: start, press: menu (back, from the track list) |
| Timing | tap: timing / map view, hold 2 s: end | press: switch view, hold 1 s: end |
| Setting the line | tap as you cross the line, hold 2 s: end | hold 1 s: end |
| Free Roam | hold 2 s: end | hold 1 s: end |
| Performance | left: rollout / demo, centre: results, right: menu, hold 2 s: menu | press: next screen, hold 1 s: menu |
| Lap session summary | left button: menu, centre button or any other tap: Ready screen again, right button: Wi-Fi page | press: Ready screen again, hold 1 s: Wi-Fi page |
| Free Roam summary | tap: menu; WI-FI button: Wi-Fi page | press: menu, hold 1 s: Wi-Fi page |
| Wi-Fi | tap: Wi-Fi off (not while firmware installs) | press: Wi-Fi off |
| Settings | tap a row, drag to scroll, top bar: back (to the menu from the list) | press: next row, hold 1 s: open or flip it (on a choice page: choose it; on Brightness and Time zone: press is +, hold goes back) |
| First-boot setup | tap the choice on the left or right | press: the other choice, hold 1 s: choose it |

While timing, the screen shows:

- **A big number** in large digits. The **Big number** setting picks it: the current lap time (the default), the live diff, or the predicted lap time. The diff and the predicted lap need a best lap; until there is one the big number is the lap time.
- **Last and best lap.**
- **Live diff:** how far ahead (green, `-`) or behind (red, `+`) your best lap you are right now, at the same point on track. It appears once you have a best lap. With the diff as the big number, the current lap time moves to the box below.
- **Split time:** for 5 s after each split, the split time and its gap to your best lap's split.
- **GPS signal bars** in the top right: green with a fix, yellow while searching, red outline with no data.
- **GPS accuracy** beside the bars, such as `±1.2m`: how far off the GPS thinks its position could be. Green up to 2 m, yellow up to 5 m, red beyond that. It only shows with a u-blox GPS.

When you complete a lap, its time shows full-screen for 4 s, in green if it's a new best.

The live diff compares your position with the path your best lap took. If you're more than 40 m off that path (`LIVE_DELTA_MAX_OFFSET_M`), for example in the pit lane, it shows `--`.

## Settings

The choices a driver makes live in **Settings**, not in the code. Change them on the device or on the Wi-Fi page. They are kept in the board's flash (NVS, apart from the program and the SD card), so they survive switching off and re-flashing.

**On the device:** tap **SETTINGS** on the menu. It is a list like the phone's Settings, in four groups (Display, Timing, GPS & Bluetooth, Recording). It is meant for the pits, before the session starts, so it is a normal touch list, not the big glove buttons of the timing screens.

- **Touch:** drag the list up and down to scroll it. Tap a switch (Screen upside down, Bluetooth, Record road drives) to flip it. Tap a row with a `>` to open its page: Speed units, Big number, Compare with and GPS mode show their options with a tick on the current one (tap one to choose it and go back); Brightness has a bar (tap a spot on it) and − and + buttons; Time zone has − and + for half an hour and shows the local time. The top bar goes back: to the list from a page, to the menu from the list. So does **Back to menu** at the end of the list.
- **BOOT (or the big button):** press moves a highlight down the list (the list scrolls with it and starts again after the last row), hold for 1 s opens or flips that row. On a page of options, press moves down and hold chooses. On Brightness and Time zone, press is + (going round at the end) and hold goes back. A line at the bottom of the screen says so when the board has no touch screen.
- **Saved as you go:** there is no save button. A change takes effect at once, except **GPS mode**, which is used from the next restart.

| Item | Choices | What it does |
|---|---|---|
| Speed units | km/h (default), mph | Every speed on the screens, including Performance, and on the Wi-Fi page. Saved files stay in km/h |
| Screen upside down | Off (default), on | For a unit mounted upside down. Draws the screen and reads touch turned round |
| Brightness | 10% to 100% in steps of 10 (default 100%) | Dims the backlight. Auto-sleep still turns it fully off |
| Compare with | Best of the day, best ever, this session (default) | **Stored for later.** The live diff only compares with this session's best lap so far, whatever is picked |
| Big number | Lap time (default), delta, predicted lap | The largest number on the timing screen. Delta and predicted lap show the lap time until there is a best lap |
| GPS mode | 20 Hz, GPS + Galileo (default); 10 Hz, all systems | 10 Hz uses all four satellite systems: more satellites, half the rate. **Applied at the next restart** |
| Bluetooth | On (default), off | The RaceBox output for phone apps. Off stops it at once |
| Time zone | UTC−12:00 to UTC+14:00 in 30-minute steps | For session file names. Shows the local time now once the GPS has a fix. A phone opening the Wi-Fi page sets it |
| Record road drives | On (default), off | Off: Free Roam drives are shown but not saved |

**First boot:** a new board asks two questions after the logo, before anything else. First, **which way up**: the question is printed at the top and again upside down at the bottom, with an arrow on each side. Tap the arrow that points up, whichever way the unit is mounted; the screen turns round at once if it was the other one. Then **km/h or mph**. With BOOT, press picks the other choice and hold chooses it. Both can be changed later in Settings.

**On the Wi-Fi page:** the **Settings** tab has the same items except the time zone, which it shows but takes from the phone. Changing anything asks for the 4-digit upload code on Track Rat's Wi-Fi screen once. A GPS mode change offers a **Restart now** button. The Bluetooth item is missing from the page on a build with Bluetooth left out.

**What a new board starts with** is set by the `DEFAULT_*` lines in `include/Config.h` (see [Configuration](#configuration)). Pins and everything else stay there.

## Big button and LED bar

Two optional add-ons on the expansion header. Both are off the screen, so they work with gloves and in sun. Neither is needed; the timer works the same without them.

### Parts

| Part | Notes |
|---|---|
| Push button | Momentary, normally open. A 25-30 mm panel-mount arcade button is good with gloves |
| WS2812B LED strip | 10 LEDs by default (8-12 works). Any 5 V "NeoPixel" type strip |
| 330-470 ohm resistor | In series with the strip's data line |
| 1 kohm resistor | In series with the button |
| 100 nF capacitor | Across the button (optional) |
| 470-1000 uF capacitor | Across the strip's 5 V and GND, close to the strip |
| 74AHCT125 or 74HCT245 | Optional 3.3 V to 5 V level shifter for the strip's data line |

### Wiring

| Add-on | Connect | Pin |
|---|---|---|
| Button | one side to IO21, the other to GND | IO21 |
| LED bar data | strip DIN to IO2 | IO2 |
| LED bar power | strip 5 V to a 5 V supply, strip GND to the board's GND | |

- **Button:** no external pull-up is needed; the board's internal one is used. Put the 1 kohm resistor in series with the button. IO21 is also a GPS pin, and the board briefly drives it at start-up while it looks for the GPS, so the resistor stops a button held down at that moment from shorting the pin. The 100 nF across the button smooths contact bounce, but isn't required, as the firmware debounces too.
- **LED bar power:** the strip needs 5 V, not 3.3 V. Each LED draws up to 60 mA at full white. The default brightness (`LED_BAR_BRIGHTNESS` 64 of 255) keeps 10 LEDs under about 200 mA. Power the strip from a 5 V supply that isn't the board's 3.3 V pin, and join its GND to the board's GND. Check your board's header pinout for a 5 V pin before using it for the strip.
- **LED bar data:** the board's 3.3 V output is a little under what a WS2812B strictly wants (0.7 x 5 V = 3.5 V), but it normally works on a short wire. If the strip flickers or shows random colours, use the level shifter. Keep the data wire short, and put the 330-470 ohm resistor right at the strip's DIN end.
- **Strip capacitor:** the 470-1000 uF capacitor stops the switch-on surge from upsetting the strip. Connect it before powering up, and always connect GND before 5 V.
- **Mounting:** DIN is the end of the strip with the arrow pointing away from it. Mount it so the first LED is at the left (from the driver's seat). If "ahead" grows to the left on your strip, set `LED_BAR_REVERSED` to `1`.
- **Board LED:** the board's own RGB LED (IO42) needs no wiring.

### Button

The button does exactly what BOOT does: a short press is "next / switch view" and a hold of 1 s is "select / start / end the session". See the BOOT column in the table under "Using it".

### LED bar

The bar lights up while a lap is running and the best lap's delta is known. The middle of the strip is zero:

- **Ahead of your best lap:** green LEDs grow to the right from the middle.
- **Behind your best lap:** red LEDs grow to the left from the middle.
- **How many:** the whole side is lit at 1 s ahead or behind (`LED_BAR_FULL_SCALE_MS`), with at least one LED once you're more than 0.02 s out. A bigger gap clamps at a full side.
- **On the pace** (within 0.02 s): a dim green marker in the middle.
- **New best lap:** the whole bar flashes purple for about 2 s.
- **Each split:** a short white blink (0.2 s).
- **Timing but no delta yet** (before your first lap is done, or off the best lap's line): a dim amber marker in the middle.
- **Not timing** (menus, summaries, no GPS fix): off.

The board's own LED shows the same colour (green ahead, red behind, brighter for a bigger gap, purple and white for the same events), so you can test the logic without any strip. It's easiest to see with the bench test.

The LEDs are updated 40 times a second, twice the GPS rate. The strip is driven by the ESP32-S3's RMT peripheral, which sends the colours by itself, so the main loop doesn't wait for it. With no strip fitted, nothing happens and nothing breaks.

### If the GPS is on IO14/IO21

The GPS can be plugged into the expansion header instead of the UART connector (see Wiring above). It then uses IO21, so the button can't be used, and the log says so:

```
Input: big button disabled - the GPS is on IO21
```

The same check covers every add-on pin: any add-on on a pin the GPS ended up on stays off. The LED bar on IO2 and the board LED are unaffected by a GPS on IO14/IO21.

### Add-on options

These are set in `include/Config.h`.

| Option | Default | What it does |
|---|---|---|
| `EXT_BUTTON_ENABLED`, `EXT_BUTTON_PIN` | 1, 21 | The big button |
| `LED_BAR_ENABLED`, `LED_BAR_PIN` | 1, 2 | The LED strip |
| `LED_BAR_COUNT` | 10 | LEDs on the strip (2 to 64) |
| `LED_BAR_BRIGHTNESS` | 64 | 0-255. Turn it up in sun, down for less current |
| `LED_BAR_REVERSED` | 0 | 1 = flip left and right |
| `LED_BAR_FULL_SCALE_MS` | 1000 | Delta that lights a whole side of the bar |
| `BOARD_LED_ENABLED`, `BOARD_LED_PIN`, `BOARD_LED_BRIGHTNESS` | 1, 42, 40 | The board's own LED |

## Acceleration and braking

Pick **PERFORMANCE** on the menu. Use a flat, straight road or a drag strip.

- **READY / GO:** stop for 1 s (under 1 km/h) and the screen shows READY in green. Pull away and it shows GO, with the elapsed time and distance. Timing starts when the car starts to move, worked out from how fast speed is rising.
- **What's timed:** 0-60 and 0-100 km/h, 0-60 mph, 100-200 km/h, and the 1/8 and 1/4 mile (elapsed time and the speed at the end of each). Times are worked out between GPS fixes, so they're good to a few milliseconds at 10 or 20 Hz. Distance comes from adding up GPS speed.
- **100-200 km/h** is also timed from a rolling start, when you haven't stopped first.
- **Braking:** the distance and time from the moment you slow through 100 km/h to a stop. It works from a rolling start, so you can brake whenever you like.
- **1 FT:** the left button turns on a 1-foot rollout. The clock then starts once the car has moved a foot, as at a drag strip, so times come out about 0.3 s shorter. Distances are still measured from where the car started to move. A tag at the top of the screen shows when it's on.
- **Result cards:** a run ends when you lift (speed drops 10 km/h below its peak), stop, or hold a steady speed for 3 s. Its card then appears, with the times that were reached and `--` for the rest. PREV steps back through the runs, BEST shows the best of each measurement this session, and LIVE goes back to the speed screen. The next launch switches back to live by itself.
- **INVALID:** the GPS dropped fixes during the run (more than 0.4 s apart), so the times can't be trusted. The card says so and the run doesn't count towards the best.
- **NOT LEVEL:** the height changed by 3 m or more, and by 1.5% or more of the distance, so the road wasn't flat. GPS height is only good to a few metres, so gentle slopes aren't flagged. Times are not corrected for slope, and a not-level run still counts towards the best.
- **Units:** the **Speed units** [setting](#settings) (km/h or mph) drives these screens too. Turn on `PERF_ROLLOUT_DEFAULT` in `include/Config.h` to start with the rollout on.
- **Buttons are ignored while moving** (over 5 km/h), so a bump can't end a run. Hold the screen or BOOT to leave.
- **Demo:** with no GPS fix, the left button reads DEMO. It starts a simulated car that stands for 4 s, launches to 215 km/h, brakes to a stop and repeats, so you can try the screens at a desk. The yellow BENCH tag shows while it runs.

Each run is saved to `/sessions/<date>_<time>_perf.json`, rewritten after every run (see [Session logs](#session-logs)).

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
      "startFinish": { "lat": 40.123456, "lon": -74.123456, "radius": 15, "heading": 90 },
      "splits": [
        { "lat": 40.124500, "lon": -74.120100, "radius": 15, "heading": 180 }
      ]
    }
  ]
}
```

- **`startFinish` and `splits`:** a point on the racing line. A lap or split counts when your path passes through that point, within `radius` metres either side.
- **`heading`:** the direction of travel across the line, in degrees (0 = north, 90 = east, 180 = south). A crossing counts only if you're heading within 90° of it, so a straight alongside the line or the pit lane, running the other way, doesn't end a lap. This matters most on compact kart tracks. Optional: without it, crossings either way count. If the track has a map, the direction is taken from the map near the line instead, and if that turns out backwards (you cross the line the "wrong" way twice in the same place), it's turned round.
- **`radius`:** about half the track width plus a few metres. 10–20 m is typical.
- **Placement:** put splits where no other part of the track passes within `radius`, or give them a `heading`.
- **`splits`:** optional.
- **No start/finish yet:** a track can have a `"center": {"lat": .., "lon": ..}` instead of `startFinish`. It's still found automatically, and the timer asks you to tap at the line the first time.
- **Saved by the timer:** new tracks, start/finish lines marked on track (with their direction), and maps from your first lap are written back into `tracks.json` on the SD card (maps when the session ends). It's saved compactly, one track per line.

### Layouts

A venue with more than one layout lists them in `layouts`. Each has a `name`, and its own `startFinish`, `splits`, `map` and (optionally) `length`; the venue's own `startFinish`, `splits` and `map` are then ignored. Venues without `layouts` work as before.

```json
{
  "id": "my-kart-park",
  "name": "My Kart Park",
  "center": { "lat": 40.123, "lon": -74.123 },
  "layouts": [
    { "name": "Full", "length": 1200,
      "startFinish": { "lat": 40.123456, "lon": -74.123456, "radius": 12, "heading": 90 } },
    { "name": "Reverse", "length": 1200,
      "startFinish": { "lat": 40.123456, "lon": -74.123456, "radius": 12, "heading": 270 } },
    { "name": "Short", "length": 700,
      "startFinish": { "lat": 40.123456, "lon": -74.123456, "radius": 12, "heading": 90 } }
  ]
}
```

- **Records are kept per layout.** Each layout's id is `<venue id>.<layout id>` (here `my-kart-park.full`), used for session file names. A layout's id is its name in lower case with dashes, unless you give it an `"id"`.
- **Telling layouts apart:** the timer needs a `map` for each layout to recognise it from your lap (see Track maps). A Reverse layout with the same map is told apart by the `heading` of its line.
- **Switching mid-session:** if the new layout shares the start/finish line, your laps so far are kept. Otherwise the session so far is saved under the old layout, and timing starts again at the new layout's line, in a new session file.

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

## Screen design and fonts

Every screen uses **Inter**, an open-source typeface (SIL Open Font License, see `include/fonts/OFL.txt`) drawn in the style of Apple's San Francisco. Text is anti-aliased, and digits are tabular, so a running lap time doesn't shift sideways as it counts. Big numbers use Inter Display, its tighter cut for large sizes. The Wi-Fi page uses the same typeface, served from the board (`web/fonts/`), since the phone has no internet on Track Rat's network.

- **Colours and layout:** `include/UiTheme.h`. True black, raised grey cards, and one bright colour per meaning: green ahead or best, red behind or no fix, yellow waiting, orange go, blue Wi-Fi and Bluetooth. Text on a coloured block is black, which holds up better in sun.
- **Changing a font size or weight:** edit `FONTS` in `tools/make_fonts.py` and run it. It needs Inter's `.otf` files (the [release zip](https://github.com/rsms/inter/releases), or `fonts-inter` on Debian/Ubuntu) and `pip install pillow fonttools`. It rewrites `include/fonts/*.h`; rebuild and flash.

### Seeing the screens without the board

`tools/render_screens.py` builds the real screen code on your PC against a stand-in for the display library, drives a simulated car round a track from `data/tracks.json` through the real lap timer, and saves every screen as a PNG, plus a contact sheet of them all:

```bash
pip install pillow
python tools/render_screens.py        # -> build/screens/*.png, build/screens/contact_sheet.png
```

Text is placed and drawn as on the board, with the same fonts; shapes are close but not pixel-identical. Needs g++.

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
- **`--heading`:** the direction of travel across the start/finish (0 = north, 90 = east). The script prints which way the map runs at the line, which is usually, but not always, the racing direction. Without `--heading`, the timer works the direction out from the map.
- **`--layout NAME`:** adds or updates one layout of a venue. Run the script once per layout with the same `--id`. If the venue already had a single layout, it's kept as a layout called `Main`.

## Phone apps (Bluetooth)

Track Rat sends its GPS over Bluetooth in the same format as a **RaceBox Mini**, so apps that support that device can use it as their GPS. The board's own screen and lap timing keep working at the same time.

- **Apps with RaceBox Mini support:** RaceChrono and RaceChrono Pro, Solostorm, NMEAconnect, AutoX DL and the RaceBox app. This list comes from the [RaceBox Mini emulator project](https://github.com/anchit92/Open-Source-RaceBox-mini-Emulator) that inspired this feature; I've not tested each app.
- **Connecting:** in the app, add a RaceBox Mini device. The board shows up as `RaceBox Mini` followed by 10 digits (the log prints the exact name at boot). While a phone is connected, a blue **BT** tag shows in the header.
- **Update rate:** one packet per GPS fix, so up to 20 a second (10 in the 10 Hz [GPS mode](#settings)).
- **Accelerometer:** this board has none, so the motion values read as a device lying still. Apps still calculate G-forces from the GPS.
- **Accuracy figures:** the position, speed and heading accuracy fields come straight from the GPS (UBX-NAV-PVT). With a GPS that only sends NMEA they're estimates from the satellite precision value (HDOP).
- **Bench Test:** a bench session streams the simulated car, which is handy for setting up an app at a desk.
- **Turning it off:** set **Bluetooth** to Off in [Settings](#settings). It stops at once and stays off until you turn it on. To leave Bluetooth out of the build altogether, set `BLE_ENABLED` to `0` in `include/Config.h`; the Bluetooth item then disappears from the Wi-Fi page.

Track Rat isn't affiliated with RaceBox. The device name is used only so apps recognise the data format.

## Battery and sleep

Plug a 3.7 V LiPo into the board's battery socket (1.25 mm, 2 pins) and the unit keeps running when the car's USB power goes off. The board charges it from USB.

- **Battery percentage:** shown in the top bar next to the GPS bars. It turns yellow at 30% and red at 15%. With no battery fitted, nothing is shown.
- **It's an estimate.** The percentage comes from the battery voltage, which reads high while charging and dips under load.
- **How it's measured:** the board feeds half the battery voltage to IO9 (two 200 k resistors, from the board's schematic). The firmware averages 16 readings a second and smooths them. If the figure is off from a multimeter, set `BATTERY_CAL_OFFSET_MV`.
- **No battery fitted:** a reading under 2.5 V, or over 4.23 V (the charger holds an empty socket at about 4.2 V), for 5 s in a row counts as "no battery". If a missing battery still shows a percentage, or a full one disappears, adjust `BATTERY_PRESENT_MAX_MV`. The serial log prints the readings every 10 s.
- **Low battery:** at 3.30 V or below for 10 s, Track Rat saves the session, shows **BATTERY LOW** for 5 s, then goes to sleep. Charge it, then touch the screen or press BOOT.

**Auto-sleep:** parked with no touch and no BOOT press for 20 minutes, the backlight goes off. This is a full off, whatever the Brightness setting; the chosen level comes back when the screen wakes. A minute later the board goes into deep sleep. Moving at 5 km/h or more counts as activity, so it never sleeps during a session.

- **Waking:** touch the screen or press BOOT. From backlight-off, the press only turns the screen on. From deep sleep, the board restarts as if just powered on, so it finds the GPS again.
- **Saving:** a session in progress is saved before sleeping, the same as ending it.
- **Not measured yet:** how much current the board draws asleep, and whether the GPS module stays powered.
- **Turning it off:** set `AUTO_SLEEP_ENABLED` to `0` in `include/Config.h`.

## Updating the firmware from the SD card

You can update without PlatformIO or a USB cable.

1. Build with `pio run -e es3c28p` (or get a `firmware.bin` from whoever built it).
2. Copy `.pio/build/es3c28p/firmware.bin` to the root of the SD card and rename it `trackrat.bin`. Don't use `bootloader.bin` or `partitions.bin`.
3. Put the card in and switch on. After the logo, a screen offers to install it. Tap **INSTALL** (or hold BOOT for 1 s). Anything else skips, and so does waiting 30 s.
4. A progress bar shows while it writes (a few seconds). Don't switch it off. Then the board restarts on the new firmware.

- **After installing:** the file is renamed `trackrat.installed.bin` (replacing any older one), so it isn't offered again.
- **Wrong file:** a file that isn't an ESP32-S3 app image, or is too big, is refused with a message and left on the card.
- **Safety:** it writes to the second app slot, and the board only switches to it once the whole image has been written and checked. If the write fails, the old firmware keeps running. Sessions and tracks are not touched.
- **If a new build won't start,** flash over USB with `pio run -e es3c28p -t upload`.
## Wi-Fi page

Get your sessions onto your phone without taking the SD card out.

1. In the pits, tap **WI-FI** on the menu or on a session summary (or hold BOOT on a summary). The screen shows the network name, the address and an upload code.
2. On your phone, join the Wi-Fi network **TrackRat-XXXX**. There's no password. The last 4 characters are different on each board.
3. The page opens by itself, like a hotel Wi-Fi login. If it doesn't, open **http://192.168.4.1** in your browser.
4. When you're done, tap the Track Rat screen (or press BOOT) to turn Wi-Fi off.

What the page does:

- **Sessions:** every file in `/sessions`, by day and track, newest first. Tap a file to download it. Files from the same session (`.json`, `.vbo` and so on) are shown together. Times are in your phone's time zone.
- **Tracks:** download the `tracks.json` Track Rat is using, or upload an edited one. The old one is kept as `tracks.json.bak`, and Track Rat restarts to load the new list. If power is cut mid-save (here or on the device) and `tracks.json` is gone, the next boot puts back the newest complete copy it finds (`.tmp`, `.upload`, then `.bak`).
- **Firmware:** install a new `firmware.bin` (from `.pio/build/es3c28p/` after `pio run`). The page and Track Rat both check the file first, as the SD card update does: it must be an ESP32-S3 app image that fits the slot, so a `bootloader.bin` or a file for another chip is refused before anything is written. A progress bar shows while it's sent; then Track Rat restarts with Wi-Fi off. Keep it powered until it does.
- **Device:** firmware version, free space on the card and in flash, GPS status and memory.
- **Settings:** the same items as the Settings screen on Track Rat; see [Settings](#settings). Changing one asks for the upload code and takes effect at once, except the GPS mode, which waits for a restart (the page offers one). The time zone is shown but not changed here.

Each time the page opens it tells Track Rat your phone's time zone, which becomes the **Time zone** setting. From then on, session files are named in your local time instead of UTC. Daylight saving time changes the zone, so it's correct again the next time a phone opens the page. You can also set it on the Settings screen, but the next phone to open the page replaces it.

Good to know:

- **Wi-Fi is off while timing.** It's only on while the Wi-Fi screen shows, and starting a session turns it off. Bluetooth (RaceBox) keeps working while it's on.
- **Downloads in the sign-in window:** the window phones open by themselves often can't save files. If a download doesn't start, open http://192.168.4.1 in Safari or Chrome while still on the TrackRat network.
- **Mobile data:** some phones keep using mobile data for everything when a Wi-Fi network has no internet. If the page won't load, tell the phone to stay connected or turn mobile data off.
- **Upload code:** installing firmware, replacing `tracks.json` or changing a setting asks for the 4-digit code on Track Rat's Wi-Fi screen, so only someone who can see the screen can do it. After 5 wrong codes, uploads are locked until Wi-Fi is turned off and on again (which also gives a new code). Downloads don't need it.
- **Anyone nearby can join** and download sessions while it's on, so turn it off when you're done.

### Changing the page

The page is a single file, `web/index.html` (plain HTML and JavaScript, no libraries), plus its fonts in `web/fonts/`. `tools/build_web.py` gzips the page into `data/web/index.html.gz` and copies the fonts to `data/web/fonts/` every time PlatformIO runs; it also puts the git version on the Device tab. To put a new page on the board:

```bash
pio run -e es3c28p -t uploadfs
```

Without it, the board shows a plain page with the session list, `tracks.json` and firmware upload. There is no Settings tab then; use the Settings screen on the board.

## Session logs

Sessions are saved in `/sessions` on the SD card, as two files with the same name. For a venue with layouts, the track id includes the layout (`calabogie.full`) and the summary also names the layout.

- **`<date>_<time>_<track id>.vbo`:** every GPS fix, up to 20 a second (10 in the 10 Hz GPS mode): UTC time, position, speed, heading, height, satellites, HDOP (GPS precision), lap number, and lateral and longitudinal g worked out from the GPS (+ = turning right, + = speeding up). About 6.5 MB per hour of driving.
- **`<date>_<time>_<track id>.json`:** the summary: every lap and split time, max and average speed per lap, the best lap, and the name of its `.vbo`.

File names use UTC until the **Time zone** setting is set, either on the Settings screen or by a phone opening the Wi-Fi page. Then they use your local time. Times inside the `.vbo` are always UTC. Speeds in the files are always km/h, whatever the Speed units setting.

Free Roam drives are saved the same way, as `<date>_<time>_freeroam.vbo` and `.json`, unless **Record road drives** is off in [Settings](#settings): then nothing is saved. The summary holds the duration, distance, and top and average speed.

**Without an SD card,** only the summaries are kept, in flash (a full log fills it in about half an hour). Free Roam then saves its route as GPS points about 10 m apart in the summary. The log at boot says when there's no card.

### Opening a session in Circuit Tools

[Circuit Tools](https://vboxmotorsport.co.uk/us/circuit-tools) is Racelogic's free analysis software for Windows, macOS and iOS. It shows lap overlays and speed and time-gap graphs.

1. Copy the `.vbo` from the card's `sessions` folder to your computer.
2. Open it in Circuit Tools.
3. **If the track has a map** in `tracks.json`, the file includes the start/finish line and any splits, so the laps appear straight away. **Otherwise** (new tracks, a line you marked during the session), set the start/finish line in Circuit Tools. Everything you drove is in the file either way.

### If the power goes off

The ignition can cut the power at any moment, so nothing needs to be ended to be kept:

- **The log** is written about once a second, so a power cut loses at most the last second.
- **The summary** is rewritten after every lap without ever being half written: the new copy goes to a temporary file, which then replaces the old one.
- **At the next boot,** any session that wasn't ended gets its summary rebuilt from its log, by timing the log again with the same lap timer and the same lines (the log records each line's direction too). A session that switched to another layout at the same start/finish is timed with that layout's lines and kept under its name, as its summary was. The log at boot shows `Session: recovered ...`.

Performance runs are saved as `/sessions/<date>_<time>_perf.json`, with every run's times (seconds), trap speeds (km/h), braking distance (metres), peak speed, height change, and whether it was invalid or not level, plus the session's best. It's rewritten after every run the same safe way as the summary.

## Configuration

What a driver chooses (units, brightness and so on) is a [setting](#settings), changed on the device, not here. The `DEFAULT_*` lines below are only what a new board starts with; after that the saved setting wins. Everything else is in `include/Config.h`. The options you're most likely to change:

| Option | Default | What it does |
|---|---|---|
| `DEFAULT_SPEED_MPH` | 0 | Starting Speed units: 1 = mph |
| `DEFAULT_BRIGHTNESS_PCT` | 100 | Starting Brightness, 10 to 100 |
| `DEFAULT_COMPARE_TO` | 2 | Starting Compare with: 0 = best of the day, 1 = best ever, 2 = this session |
| `DEFAULT_BIG_NUMBER` | 2 | Starting Big number: 0 = delta, 1 = predicted lap, 2 = lap time |
| `DEFAULT_UTC_OFFSET_MIN` | 0 | Starting Time zone, in minutes east of UTC |
| `DEFAULT_RECORD_ROAD_DRIVES` | 1 | Starting Record road drives: 0 = off |
| `DISPLAY_ROTATION` | 1 | The "normal" screen direction (1 or 3); the Screen setting's upside down uses the other |
| `BLE_ENABLED` | 1 | 0 = leave Bluetooth out of the build. Otherwise the Bluetooth setting starts on |
| `GPS_PIN_PAIRS` | 44/43, 43/44, 14/21, 21/14 | Pins searched for the GPS at boot |
| `GPS_TARGET_RATE_HZ` | 20 | The "20 Hz" GPS mode: requested update rate (u-blox only). The GPS Status screen shows the rate actually reached |
| `GPS_USE_GALILEO` / `_GLONASS` / `_BEIDOU` | 1 / 0 / 0 | The "20 Hz" GPS mode's satellite systems besides GPS. More systems = more satellites but a lower top rate (4 systems: 10 Hz, 2: 20 Hz). The "10 Hz" mode always uses all four |
| `GPS_USE_UBX` | 1 | Read a u-blox GPS as binary UBX-NAV-PVT. 0 = always read NMEA |
| `GPS_ECHO_NMEA` | 0 | 1 = copy raw GPS output to the serial monitor, and leave NMEA on alongside UBX |
| `DISPLAY_INVERT` | 1 | Set to 0 if colours look inverted (white background) |
| `TOUCH_SWAP_XY`, `TOUCH_INVERT_X/Y` | 1, 0, 1 | Fix touch orientation if taps land in the wrong place |
| `MIN_LAP_TIME_MS` | 20000 | Start/finish crossings sooner than this are ignored |
| `PERF_ROLLOUT_DEFAULT` | 0 | 1 = Performance starts with the 1-foot rollout on |
| `TRACK_DETECTION_RADIUS_M` | 5000 | Range for automatic track selection |
| `AUTO_START_KMH`, `AUTO_START_HOLD_MS` | 20, 2000 | A track picked at boot with the car parked starts its session when the car stays at this speed or more for this long. Already at this speed at boot: starts at once |
| `BATTERY_ENABLED` | 1 | 0 = ignore the battery pin completely |
| `BATTERY_CAL_OFFSET_MV` | 0 | Added to the battery voltage reading, to match a multimeter |
| `BATTERY_PRESENT_MIN_MV` / `_MAX_MV` | 2500 / 4230 | Readings outside this range mean no battery is fitted |
| `BATTERY_LOW_MV`, `BATTERY_LOW_HOLD_MS` | 3300, 10000 | Save and sleep at or below this voltage for this long |
| `AUTO_SLEEP_ENABLED` | 1 | 0 = never sleep by itself |
| `AUTO_SLEEP_IDLE_MS` | 20 minutes | Parked and untouched this long: backlight off |
| `AUTO_SLEEP_DEEP_AFTER_MS` | 60 s | Backlight off this long: deep sleep |
| `AUTO_SLEEP_MOVING_KMH` | 5 | At or above this speed the car counts as moving |
| `FIRMWARE_FILE` | `/trackrat.bin` | File on the SD card offered as a firmware update |

## Tests

GitHub Actions (`.github/workflows/ci.yml`) runs all of the tests below on Linux for every push and pull request, and also builds the firmware and the filesystem image. The built `firmware.bin` is attached to the run. When you add a test, add its command to the workflow too.

The lap-timing logic has no Arduino dependencies and is tested on a PC:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_laptimer.cpp src/LapTimer.cpp -o /tmp/lt && /tmp/lt
```

On Windows, add `-D_USE_MATH_DEFINES` (any of these test commands needs it there).

It checks that the timer:

- measures known-length laps to within 2 ms at both 10 Hz and 1 Hz, including laps that run past UTC midnight
- times splits correctly
- when one GPS fix crosses a split and the start/finish (a split just before or after the line, lap 1's start, several splits at once), reports every crossing in the order it happened, with a split before the line in the lap that ends and one after it in the new lap
- tracks the best lap and computes deltas
- computes the live diff to within about 40 ms of the true value
- doesn't record false laps while parked on the line with GPS jitter
- ignores a line crossing that happens during a GPS dropout
- ignores a start/finish or split crossed the wrong way, still times one crossed the right way, and treats a line with no direction as before
- on a kart track whose return straight passes 10 m from the line, times full laps instead of half laps
- turns round a direction guessed backwards from the map, but never one that was set
- recognises which layout was driven by comparing the lap with each layout's map
- in a simulated session at a venue with Full, Short, Reverse and a kart track next door, switches to the layout actually driven (after the first lap, or after 2 km if the picked layout's line is never crossed), keeps lap 1 when the line is shared, stays put when nothing fits, and still works with 2 m of GPS wander

`test/test_racebox.cpp` checks the Bluetooth packet's fields, checksum and GPS time-of-week:

```bash
g++ -std=c++17 -Iinclude test/test_racebox.cpp src/RaceBoxPacket.cpp -o /tmp/rb && /tmp/rb
```

`test/test_trackorder.cpp` checks the track list order: nearest first, layouts of one venue together at the distance of its closest layout, `tracks.json` order with no GPS fix, and Bench Test last:

```bash
g++ -std=c++17 -Iinclude test/test_trackorder.cpp -o /tmp/to && /tmp/to
```

`test/test_localtime.cpp` checks that session file names in local time roll the date over correctly: either side of midnight, across months, years and leap days, and in half-hour time zones:

```bash
g++ -std=c++17 -Iinclude test/test_localtime.cpp -o /tmp/ltz && /tmp/ltz
```

`test/test_portal.cpp` checks the Wi-Fi page's request rules: which file names can be downloaded, the session list sent a piece at a time, time zones, the captive-portal host check, the network name, upload progress, the `tracks.json` check, one upload at a time and the upload code:

```bash
g++ -std=c++17 -Iinclude test/test_portal.cpp -o /tmp/wp && /tmp/wp
```

`test/test_web.js` checks how the page groups session files by day and track, that its firmware file check says the same as the board, and that its Settings tab uses the board's field names and values (needs Node.js):

```bash
node test/test_web.js
```

`test/test_settings.cpp` checks the settings rules (defaults, limits, stepping, units, big number, predicted lap, brightness, time zone), the Settings list (where its rows are, what a tap hits, scrolling, the pages, touch and BOOT) and the first-boot questions, the field names the Wi-Fi page uses, and a simulated first boot followed by a lap session:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_settings.cpp src/LapTimer.cpp -o /tmp/st && /tmp/st
```

`test/test_freeroam.cpp` checks Free Roam's distance, top and average speed, that GPS jitter and position glitches add no distance, and that very long drives are capped:

```bash
g++ -std=c++17 -Iinclude test/test_freeroam.cpp -o /tmp/fr && /tmp/fr
```

`test/test_ubx.cpp` checks the UBX reader: checksums, every NAV-PVT field from sample messages, millisecond time (including either side of midnight), messages split across reads with NMEA in between, and messages too long to keep:

```bash
g++ -std=c++17 -Iinclude test/test_ubx.cpp src/UbxParser.cpp -o /tmp/ubx && /tmp/ubx
```

`test/test_gps_source.cpp` checks the choice between UBX and NMEA, and runs a simulated GPS through the UBX reader into the lap timer: switching on from power-up, after a board reset, after the GPS restarts, a GPS plugged in late, a GPS that isn't u-blox, damaged messages, a busy main loop, and laps across midnight:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_gps_source.cpp src/GpsSource.cpp src/UbxParser.cpp src/LapTimer.cpp -o /tmp/gs && /tmp/gs
```

`test/test_vbo.cpp` checks the session log:

```bash
g++ -std=c++17 -pthread -DDEBUG_MODE=0 -Iinclude test/test_vbo.cpp src/VboFormat.cpp src/SessionReplay.cpp src/LapTimer.cpp -o /tmp/vbo && /tmp/vbo
```

- positions in all four hemispheres and across the Greenwich meridian, UTC time across midnight, and the file's sections, channels and line endings
- g-forces from the GPS on a straight and in a turn
- the timing lines written for Circuit Tools
- that a logged session, read back and timed again, gives the same laps to within 1 ms, in Free Roam too
- recovery after a power cut: a last line cut short, zeros left on the card, a line marked during the session, a switch to another layout at the same line
- that a line's direction is recorded and timed again the same way (set the wrong way, or guessed backwards from the map)
- the log writer's buffer, with two threads, wrapping and filling up

**On Windows** without g++, the `ziglang` Python package has a C++ compiler that needs no install: `pip install ziglang`, then use `python -m ziglang c++` in place of `g++`, add `-D_USE_MATH_DEFINES`, and write the output to a `.exe`.

`test/test_power.cpp` checks the battery voltage to percentage curve, that a missing battery shows nothing and never counts as low, that a brief dip doesn't trigger low battery, the 20-minute sleep timing (movement and touches reset it), and that firmware files, from the SD card or sent over Wi-Fi in chunks of any size, are accepted only if they're an ESP32-S3 app image:

```bash
g++ -std=c++17 -Iinclude test/test_power.cpp -o /tmp/pw && /tmp/pw
```
`test/test_deltaleds.cpp` checks the LED bar pattern: which side and how many LEDs light for a given delta, the clamping at a full side, the purple best-lap flash and white split blink (and their priority), a reversed or odd-length strip, that the LED clock handles `millis()` wrapping, and which add-on pins the GPS takes:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_deltaleds.cpp src/DeltaLeds.cpp src/LapTimer.cpp -o /tmp/dl && /tmp/dl
```
`test/test_autostart.cpp` checks the drive-off detector that starts a session from the Ready screen: parked GPS noise never starts one, a short burst of speed doesn't, steady driving does after about 2 seconds, slowing down or a gap in the fixes starts the count again, and it works across midnight at 10 and 20 Hz:

```bash
g++ -std=c++17 -Iinclude test/test_autostart.cpp -o /tmp/as && /tmp/as
```

`test/test_perfrun.cpp` checks acceleration and braking runs with made-up GPS fixes at 10 and 20 Hz:

```bash
g++ -std=c++17 -Iinclude test/test_perfrun.cpp -o /tmp/pr && /tmp/pr
```

It checks that:

- known 0-60, 0-100, 100-200, 1/8 and 1/4 mile times and trap speeds come out within 1.5 ms for a constant acceleration, whichever moment the fixes fall on, and within a few ms for a car whose acceleration fades
- the 1-foot rollout takes the right time off
- 100-0 km/h braking distance and time are right to a few centimetres and milliseconds
- a rolling 100-200 run is timed without a launch
- a run across a 1.2 s GPS gap is invalid, and one across 150 ms is not
- a 5% hill is flagged not level, and noisy height on a flat road is not
- the car must be stopped for 1 s before a launch counts, and a creep forward doesn't count as a run
- a run that crosses UTC midnight is timed correctly

`test/test_jsontext.cpp` checks the `tracks.json` text helpers: counting its values (to size the document it's parsed into), with strings, empty arrays and pretty-printing, and starting a line for each track when it's saved:

```bash
g++ -std=c++17 -Iinclude test/test_jsontext.cpp -o /tmp/jt && /tmp/jt
```

## Layout

```
include/   Config.h, GeoMath.h (gate maths), LayoutMatch.h (layout recognition),
           LogRing.h (log buffer), UiTheme.h (screen size, colours),
           SettingsData.h (the settings and their rules), SettingsFlow.h (how keys move through
           the Settings and first-boot screens), headers
src/       main.cpp (state machine), GPSHandler, UbxParser, GpsSource, LapTimer, TrackDatabase,
           DisplayManager, InputManager, Storage, SessionLog, JsonFile (saving JSON safely),
           VboFormat (log format), LogWriter (writes the log on core 0),
           SessionReplay (times a session again from its log),
           Power (battery, sleep), FirmwareUpdate (SD card update)
           LedBar, DeltaLeds,
           PerfRun (acceleration/braking), PerfLog (performance summaries),
           DisplayPerf (performance screens), DisplayPower (battery and update screens),
           DisplaySettings (Settings and first-boot screens)
           Settings (kept in flash)
           WifiPortal (Wi-Fi page)
include/fonts/  the screen fonts (Inter), made by tools/make_fonts.py
web/       the Wi-Fi page and its fonts (copied into data/web when building)
data/      tracks.json
test/      host-side tests (check.h: shared by all of them)
```
