# Development

Configuration, tests, the code layout, screen design and the track tools. Back to the [README](../README.md).

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

While timing, press the screen (or BOOT) to switch between the timing screen and a **map view**. The map view shows the track layout with your car as a white dot and a **ghost**, a grey ring where your reference lap was at this moment into the lap, plus the lap time, live diff, best and last lap down the right-hand side.

- **Tracks with a `"map"` in `tracks.json`:** the stored layout is drawn.
- **Tracks without one:** the map draws itself from your GPS path. It fills in during your first lap and then shows your best lap's line.

`tools/fetch_track_map.py` adds a track's real layout from OpenStreetMap. Run it on your PC; it needs Python 3, no extra packages. For example, Calabogie:

```bash
python tools/fetch_track_map.py --id calabogie --name "Calabogie Motorsports Park" \
    --location "Calabogie, ON" --country Canada --length 5050 --sf LAT,LON
```

- **`--sf`:** the start/finish line. In Google Maps satellite view, right-click the start/finish line and click the coordinates to copy them.
- **What it does:** downloads every `highway=raceway` way within 2.5 km, simplifies it, prints a text preview of the shape, and adds or updates the entry in `data/tracks.json`. Copy that file to the SD card.
- **Pit lane:** ways tagged as pit lanes (`service=pit_lane`, or "pit" in the name) are kept and marked with `"pit"` (which lines of `"map"` they are), so Track Rat shows the slow screen in the pit lane. Add `--no-pit` to leave them out.
- **Your own layout:** `--file layout.geojson` (or `.gpx` / `.kml`) uses a file you've drawn or exported instead.
- **`--heading`:** the direction of travel across the start/finish (0 = north, 90 = east). The script prints which way the map runs at the line, which is usually, but not always, the racing direction. Without `--heading`, the timer works the direction out from the map.
- **`--layout NAME`:** adds or updates one layout of a venue. Run the script once per layout with the same `--id`. If the venue already had a single layout, it's kept as a layout called `Main`.

## Configuration

What a driver chooses (units, brightness and so on) is a [setting](user-guide.md#settings), changed on the device, not here. The `DEFAULT_*` lines below are only what a new board starts with; after that the saved setting wins. Everything else is in `include/Config.h`. The options you're most likely to change:

| Option | Default | What it does |
|---|---|---|
| `DEFAULT_SPEED_MPH` | 0 | Starting Speed units: 1 = mph |
| `DEFAULT_BRIGHTNESS_PCT` | 100 | Starting Brightness, 10 to 100 |
| `DEFAULT_COMPARE_TO` | 2 | Starting Compare with: 0 = best of the day, 1 = best ever, 2 = this session |
| `DEFAULT_BIG_NUMBER` | 2 | Starting Big number: 0 = delta, 1 = predicted lap, 2 = lap time |
| `DEFAULT_DELTA_COLOURS` | 0 | Starting Delta colours: 0 = green / red, 1 = blue / orange |
| `DEFAULT_SESSION_LENGTH` | 0 | Starting Session length: 0 = off, 1 = 20 min, 2 = 25, 3 = 30 |
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
| `PIT_PARK_KMH`, `PIT_PARK_MS` | 5, 60 s | Parked this slow for this long while timing: in the pits, the summary shows |
| `SESSION_RESUME_MS` | 10 minutes | Driving off within this of stopping carries on the same session; parked longer, the session ends |
| `SECTOR_BRAKE_DROP_KMH`, `SECTOR_MIN_MS` | 10, 3000 | Corner sectors: a braking zone loses at least this much speed; pieces shorter than this are joined |
| `SLOW_LAP_PERCENT` | 107 | A lap over this % of the best lap is tagged slow |
| `PIT_STOP_KMH` | 5 | A lap with a fix below this speed is a pit lap |
| `SUSPECT_GAP_MS`, `SUSPECT_ACCURACY_M` | 1000, 5 | A gap between fixes over this, or a fix less accurate than this, makes a lap suspect |
| `SUSPECT_DISTANCE_PERCENT`, `SUSPECT_DISTANCE_MIN_LAPS` | 5, 3 | A lap this % longer or shorter than the median makes it suspect, once there are this many laps |
| `AUTO_START_KMH`, `AUTO_START_HOLD_MS` | 20, 2000 | A track picked at boot with the car parked starts its session when the car stays at this speed or more for this long. Already at this speed at boot: starts at once |
| `BATTERY_ENABLED` | 1 | 0 = ignore the battery pin completely |
| `BATTERY_CAL_OFFSET_MV` | 0 | Added to the battery voltage reading, to match a multimeter |
| `BATTERY_PRESENT_MIN_MV` / `_MAX_MV` | 2500 / 4230 | Readings outside this range mean no battery is fitted |
| `BATTERY_LOW_MV`, `BATTERY_LOW_HOLD_MS` | 3300, 10000 | Save and sleep at or below this voltage for this long |
| `AUTO_SLEEP_ENABLED` | 1 | 0 = never sleep by itself |
| `AUTO_SLEEP_IDLE_MS` | 20 minutes | Parked and untouched this long: backlight off |
| `AUTO_SLEEP_DEEP_AFTER_MS` | 60 s | Backlight off this long: deep sleep |
| `AUTO_SLEEP_MOVING_KMH` | 5 | At or above this speed the car counts as moving |
| `POWER_OFF_HOLD_MS`, `POWER_OFF_BAR_MS` | 3000, 2000 | Holding BOOT this long switches off; its bar shows from this long. A BOOT hold let go before the bar is a 1 s hold |
| `GPS_STANDBY_IN_SLEEP` | 1 | The GPS goes into standby while the board sleeps or is off. 0 = it keeps tracking (about 20 mA) |
| `GPS_ASSISTNOW_AUTONOMOUS` | 1 | The GPS predicts satellite orbits 3 to 6 days ahead, for quick fixes after days off. 0 = leave it off |
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

`test/test_debrief.cpp` checks the theoretical best and the debrief: sector times from the gates (none for a lap that missed one), the best in each sector from clean laps only and the lap it came from, gaps that add up exactly to best lap minus theoretical best, the three biggest, the all-time bests (ignored when the gates have moved), the three kinds of hint from speed traces (and none when the lap was as good), and the per-sector paths the lap timer keeps for them:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_debrief.cpp src/LapTimer.cpp -o /tmp/db && /tmp/db
```

`test/test_sectors.cpp` builds a synthetic track with known corners (a hairpin, 90° and 45° corners, a flat-out sweeper), drives it with a realistic speed profile, and checks that each braking corner gets a cut just before its braking zone and the sweeper doesn't; that a chicane is joined, a lift doesn't cut, and nothing lands within 3 s of the line; and that laps through the real lap timer get sector times at the cuts, apart from the splits, with the gates in the VBO as T1, T2...:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_sectors.cpp src/LapTimer.cpp src/VboFormat.cpp -o /tmp/sec && /tmp/sec
```

`test/test_timing.cpp` checks the timing screen's rules (`include/TimingScreen.h`): the two colour sets, the trend bar (up for gaining, down for losing, none until it has a second of history, cleared by a lost diff, a gap in fixes or time going backwards), the sector strip's purple, green and yellow (with missed gates and sectors still to come), the flash at the line, the ghost along the reference lap of the real lap timer, and which taps switch the view while moving:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_timing.cpp src/LapTimer.cpp -o /tmp/tm && /tmp/tm
```

`test/test_situation.cpp` checks when the slow screen shows (5 s under 40 km/h, not a quick hairpin, at once in the pit lane, back when quick, across midnight), when a position counts as in the pit lane (near a line marked `"pit"` and nearer it than the track beside it), and the session clock and countdown:

```bash
g++ -std=c++17 -Iinclude test/test_situation.cpp -o /tmp/sit && /tmp/sit
```

`test/test_retime.cpp` checks re-timing: a logged session timed again with the line moved round the track (laps split in the new places), with a split or corner sectors added, from a log with no line in its header, and named after the track asked for; and the rules for which logs are re-timed and when the PB and the best of the day are replaced:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_retime.cpp src/VboFormat.cpp src/SessionReplay.cpp src/LapTimer.cpp -o /tmp/rt && /tmp/rt
```

`test/test_bestlap.cpp` checks the best-lap files (round trip, and damaged or cut-short files refused), which stored lap the Compare with setting picks, when a lap is a PB or the best of the day (never a tagged one), and the live diff from a stored lap in the real lap timer, from lap 1 until a quicker lap takes over:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_bestlap.cpp src/LapTimer.cpp -o /tmp/bl && /tmp/bl
```

`test/test_sessions.cpp` drives a simulated day at a track through the real lap timer: a red flag, every kind of glitchy lap (a GPS gap, poor accuracy, a cut across the infield, a cool-down lap), a pit stop that ends the session, and a power cut. It checks each lap's tags, that no tagged lap becomes the best or the live-diff reference (even one that only shows up as cut later), the average of the clean laps, when the pits and the end of a session are detected, the session numbers through the day, and that a summary rebuilt from the log has the same tags:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_sessions.cpp src/LapTimer.cpp src/VboFormat.cpp src/SessionReplay.cpp -o /tmp/ses && /tmp/ses
```

`test/test_logbook.cpp` checks the logbook's notes: which session names the page may save a note for, how a typed note is cleaned up (line ends, control characters, the 1000-byte limit without cutting a letter in half), the note file's JSON, and the note's lines put into a `.vbo` download after `[comments]`, sent in pieces of every size:

```bash
g++ -std=c++17 -Iinclude test/test_logbook.cpp -o /tmp/lb && /tmp/lb
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

`test/test_web.js` checks how the page groups session files by day and track (a session's logbook note kept with it), that its firmware file check says the same as the board, that its Settings tab uses the board's field names and values, and the track editor's rules (reading a .vbo, track ids, moving the line, splits in lap order, corner names, new layouts, a stage's finish), and the lap comparison (laps from a log across midnight, speed and gap against distance with lap lengths that differ, who was faster where, the season and its PBs) (needs Node.js):

```bash
node test/test_web.js
```

`test/test_settings.cpp` checks the settings rules (defaults, limits, stepping, units, big number, predicted lap, brightness, time zone, min lap time and kart everywhere), the Settings list (where its rows are, what a tap hits, scrolling, the pages, touch and BOOT, Power off asking first) and the first-boot questions, the field names the Wi-Fi page uses, and a simulated first boot followed by a lap session:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_settings.cpp src/LapTimer.cpp -o /tmp/st && /tmp/st
```

`test/test_stage.cpp` checks point to point timing: a standing start (armed at the start, the clock from the last moment standing still), splits, the finish, the drive back to the start not being a run, the live delta to the best run, timing lights a little way past where the car stopped, a rolling start, a stage with no finish (the run ends untimed when the car stops; a tap at the finish times it), a run that stops short, the finish crossed the wrong way; then a stage's `.vbo` timed again gives the same runs, re-timing with a finish set later times runs that had none, and `[laptiming]` names the lines Start and Finish:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_stage.cpp src/LapTimer.cpp src/VboFormat.cpp src/SessionReplay.cpp -o /tmp/stg && /tmp/stg
```

`test/test_fetch_tracks.py` checks `tools/fetch_canada_tracks.py`'s karting marks, from a saved OpenStreetMap result (no download): a karting venue and one with only kart raceways are kart tracks, a car circuit with a kart track beside it isn't, and a kart track already in `tracks.json` gets the mark:

```bash
python test/test_fetch_tracks.py
```

`test/test_dji.cpp` checks the DJI camera remote against DJI's own demo frames: both CRCs and the frame (the mode switch example), the connection request (three of the demo's frames), the GPS push (rebuilt byte for byte from the demo's), our GPS push from a fix (date, the +8 hour, position, height, north/east/down speed, accuracies), frames put back together from 20-byte notifications past rubbish and a damaged frame, the handshake (pairing accepted, rejected, no answer, reconnecting without the pop-up), the status push (recording, battery, too hot) and when recording started, which adverts are DJI cameras, and recording that follows sessions (drive off, park, the driver's own button, giving up after three tries):

```bash
g++ -std=c++17 -Iinclude test/test_dji.cpp -o /tmp/dji && /tmp/dji
```

`test/test_overlay.py` checks the overlay tool on a synthetic session (a 100 m circle: an out lap, a lap at 72 km/h, a slower one at 60, with engine data): reading the log (UTC from the file's date, degrees, laps, the best lap), what's shown at a moment (speed, lap time, the delta to the best lap at the same point on track, sector colours, engine values, nothing on the out lap or outside the log), each way of syncing (the camera's start and its second start, the SYNC flash's frame, an iPhone's creation date with its time zone, creation_time, a given start, a nudge), what ffmpeg says about a video, and rendered frames (the numbers and colours drawn, the car's dot on the map, see-through elsewhere, mph, no engine bars without engine data, repeated frames reused). It needs Pillow (`pip install -r tools/requirements-overlay.txt`), not ffmpeg:

```bash
python test/test_overlay.py
```

`test/test_web.js` also checks the CSV download: a row per fix, UTC across midnight, the values, lap times and deltas against the fastest or the summary's best lap, and oil temperature (empty when not known).

`test/test_tempalert.cpp` checks the engine temperatures on screen (`include/TempAlert.h`): the coolant tag's colour bands rising from cold through warm to hot, °F, and when the overheat warning shows: once at the limit, again only after dropping 3 °C below it or a minute later, nothing on stale data, oil with its own limit, coolant first when both are over, a tap closing it, and the LED bar's flashing. `test/test_settings.cpp` checks the Coolant settings (defaults, staying in order, the Wi-Fi page's names, BOOT and touch on the page) and `test/test_vbo.cpp` the laps' hottest temperatures, live and recovered from a log:

```bash
g++ -std=c++17 -Iinclude test/test_tempalert.cpp -o /tmp/ta && /tmp/ta
```

`test/test_courses.cpp` checks autocross courses as events (`include/Courses.h`, no Arduino code): which stages are courses (`"kind": "course"` with its `"event"`, old "Course 2026-10-20" names, hill climbs left alone), a new course's name and id ("Oct 25 course", "Oct 25 course 2"), choosing today's course within 5 km and the hub's most recent one, which session summaries belong to a course's day, and the day's run list across sessions (numbering, best and last, tagged runs never best). `test/test_stage.cpp` also checks taking the first run's finish tap back:

```bash
g++ -std=c++17 -Iinclude test/test_courses.cpp -o /tmp/co && /tmp/co
```

`test/test_profiles.cpp` checks the drivers and cars (`include/Profiles.h`, no Arduino code): the defaults, names (trimmed, up to 20 bytes, unique in their list), ids that are never reused, the limit of 8, removing (never the last; the current one hands over to the first), choosing, a car's VIN and finding a car by it, the label, a damaged saved list being repaired, and the best-lap folder (empty for driver 1 with car 1 so older PBs stay, else `d3-c2`). `test/test_settings.cpp` also checks the Driver and Car pages:

```bash
g++ -std=c++17 -Iinclude test/test_profiles.cpp -o /tmp/pr && /tmp/pr
```

`test/test_devicelink.cpp` checks when the OBD adapter and the camera count as connected (`include/DeviceLink.h`): the Bluetooth link must be up *and* the device must have sent something recently, so a remembered one that is away, off, unplugged or still connecting shows nothing. It covers the first message not yet in, the edge of the window, `millis()` wrapping, a camera switched off while its link is still up (the tag drops within `CAMERA_ALIVE_MS`) and a car-off adapter answering every few seconds (never blinks the tag):

```bash
g++ -std=c++17 -Iinclude test/test_devicelink.cpp -o /tmp/dl && /tmp/dl
```

`test/test_racechrono.cpp` checks the RaceChrono profile's encodings: latitude and longitude, both altitude and speed forms and where they switch (2776.7 m, 327.67 km/h), bearing (never 360.00), HDOP, the time since the hour and the date-hour, the sync bits going up with the hour and wrapping in 3 bits, the invalid values without a fix, CAN packets (the little-endian ID, 1 to 16 bytes), the value packets' equations, and the filter commands (deny all, allow all at an interval, allow one ID at its own, bad lengths ignored):

```bash
g++ -std=c++17 -Iinclude test/test_racechrono.cpp -o /tmp/rc && /tmp/rc
```

`test/test_elm327.cpp` checks the Bluetooth adapter's ELM327 conversation against a simulated adapter: the set-up commands in order, the supported-PID questions, a clone that rejects the "1" suffix or multi-PID requests, `SEARCHING...` before a slow first reply, spaces left on, replies cut into 1, 7 and 20-byte notifications, a missing prompt (a timeout, then ATZ after three), the engine stopping answering and coming back, the pedal as throttle, values stamped at the middle of their request, reading `NO DATA`, `?`, `STOPPED`, bus errors, multi-ECU, multi-PID and multi-frame replies, which scanned devices are listed, the schedule's rates at 10, 20 and 40 replies a second, and oil temperature asked about once a second only when the car lists it:

```bash
g++ -std=c++17 -Iinclude test/test_elm327.cpp -o /tmp/elm && /tmp/elm
```

`test/test_obd.cpp` checks OBD-II over CAN: the requests, reading engine speed, throttle and coolant temperature from the engine's replies while ignoring everything else on the bus (other messages, other modes, "not supported", short and multi-frame replies), values going stale, the polling schedule (engine speed every other request, one at a time, a gap between, a timeout; oil temperature once a second, dropped after three unanswered), and the shift light. `test/test_vbo.cpp` checks the log's engine channels:

```bash
g++ -std=c++17 -Iinclude test/test_obd.cpp -o /tmp/obd && /tmp/obd
```

`test/test_carobd.cpp` checks car-specific data from an OBDb car profile (`CarObd.h`) and the ELM327 client's turns with it: reading OBDb's module and request fields (11-bit, Honda's short 29-bit form, priority, extended address), decoding values (bit positions, signs, byte order, scaling, limits, "no value"), finding a reply among frames shown with headers on (long replies, a missing frame, late answers to an earlier request, other modules, refusals, "response pending"), the adapter settings sent on the way to a module and back (only those that change), choosing the car by its VIN, and whole conversations with a simulated adapter that only answers a module when it's set up for it: a BMW-style module (11-bit, extended address) at about 10 Hz with the engine still read, a Honda-style one (29-bit), an 11-bit request skipped on a 29-bit car, a refused request paused, the profile removed mid-way, an old clone without extended addressing, the ignition going off while pointed at a module, and the old conversation when there are no profiles. The replies are made up for the tests. `test/test_vbo.cpp` and `test/test_web.js` check the log's and the CSV's car channels:

```bash
g++ -std=c++17 -Iinclude test/test_carobd.cpp -o /tmp/car && /tmp/car
```

`test/test_obd_car.py` checks `tools/obd_car.py` on made-up signal sets (no download): model years, the make's commands filling in for the model's, OBDb's recorded answers (seen, not seen, unverified), and the profile it writes (only the chosen signals, the rate, the licence and sources):

```bash
python test/test_obd_car.py
```

`test/test_imu.cpp` checks the IMU maths: reading the chip's raw bytes, which chips answer, and learning the mount (up from standing still, forward from straight-line speeding up and braking) at eight different angles, flat, on its side, upside down and tilted, then the car's g and rotation rates; that corners, too little speeding up, readings that disagree, and engine shake while parked teach it nothing; and averaging between fixes:

```bash
g++ -std=c++17 -Iinclude test/test_imu.cpp -o /tmp/imu && /tmp/imu
```

`test/test_tracklearn.cpp` checks tracks that learn themselves: a loop driven twice is found, with the line where the first loop began, and the lap timer then times every loop from the kept fixes; a tight kart track whose straights run back alongside each other still counts; one lap round the block, a road driven out and back over and over, a figure of eight driven once, a short loop then a long one, a loop under 300 m, a slow car park and loops too long for the window are not tracks; a long drive keeps a bounded path; and the kept fixes stay in order:

```bash
g++ -std=c++17 -DDEBUG_MODE=0 -Iinclude test/test_tracklearn.cpp src/LapTimer.cpp -o /tmp/tl && /tmp/tl
```

`test/test_freeroam.cpp` checks Free Roam's distance, top and average speed, that GPS jitter and position glitches add no distance, and that very long drives are capped:

```bash
g++ -std=c++17 -Iinclude test/test_freeroam.cpp -o /tmp/fr && /tmp/fr
```

`test/test_ubx.cpp` checks the UBX reader: checksums, every NAV-PVT field from sample messages, millisecond time (including either side of midnight), messages split across reads with NMEA in between, messages too long to keep, and the standby request and AssistNow Autonomous status used when the board sleeps:

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

`test/test_power.cpp` checks the battery voltage to percentage curve, that a missing battery shows nothing and never counts as low, that a brief dip doesn't trigger low battery, the 20-minute sleep timing (movement and touches reset it), a BOOT press (a tap, a hold that acts when let go before 2 s, one held on to switch off doing nothing else, the big button's hold acting at 1 s, and the press that woke the board doing nothing), and that firmware files, from the SD card or sent over Wi-Fi in chunks of any size, are accepted only if they're an ESP32-S3 app image:

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
           TimingScreen.h (the timing screen's colours, trend bar, sector strip and ghost),
           SituationScreen.h (the slow screen, the pit lane, the session clock),
           Retime.h (which sessions are re-timed, when best laps are replaced),
           SettingsData.h (the settings and their rules), SettingsFlow.h (how keys move through
           the Settings and first-boot screens), ButtonPress.h (a button's presses and holds),
           Obd.h (OBD-II values and schedules), CarObd.h (car-specific requests from an OBDb car profile),
           CarProfileJson.h (reading a car profile),
           Elm327.h (the Bluetooth adapter's conversation), RaceChrono.h (RaceChrono's DIY encodings), DjiRemote.h (the DJI camera remote, ported
           from DJI's demo), DeviceLink.h (is the OBD adapter or camera really there), DeviceScreen.h (Settings > Engine data and Camera), headers
src/       main.cpp (state machine), GPSHandler, UbxParser, GpsSource, LapTimer, TrackDatabase,
           DisplayManager, InputManager, Storage, SessionLog, JsonFile (saving JSON safely),
           VboFormat (log format), LogWriter (writes the log on core 0),
           SessionReplay (times a session again from its log),
           Power (battery, sleep, power off), FirmwareUpdate (SD card update)
           LedBar, DeltaLeds,
           PerfRun (acceleration/braking), PerfLog (performance summaries),
           DisplayPerf (performance screens), DisplayPower (battery and update screens),
           DisplaySettings (Settings and first-boot screens)
           Settings (kept in flash)
           CanObd (OBD-II over CAN), ObdBle (the Bluetooth OBD-II adapter), EngineData (either),
           CarProfiles (car profiles on the SD card),
           CameraBle (the DJI camera's GPS remote), RaceBoxBle, RaceChronoBle (the phone's two profiles)
           WifiPortal (Wi-Fi page)
include/fonts/  the screen fonts (Inter), made by tools/make_fonts.py
web/       the Wi-Fi page and its fonts (copied into data/web when building)
data/      tracks.json
test/      host-side tests (check.h: shared by all of them)
tools/     overlay.py (overlay videos; requirements-overlay.txt), render_screens.py, fetch_track_map.py,
           obd_car.py (car profiles from OBDb)...
```
