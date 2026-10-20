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

A failed build is reported and nothing is flashed; it tries again when a newer commit arrives. If you have uncommitted edits to the code, it won't pull until you commit or stash them.

**Board unplugged?** It remembers the last commit that actually reached the board (`logs/.last_flashed`). Until the board's port appears it just waits, then flashes the latest commit, with `uploadfs` too if anything for the file system changed since. So nothing is missed while the board is away.

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

**Sessions run themselves.** Park (under 5 km/h) for a minute and you're in the pits: the [debrief](#the-pit-debrief) comes up (or, before there are corner sectors, the session's summary with its laps and their tags). Drive off again within 10 minutes of stopping (a red flag, a quick check) and the same session carries on, back on the timing screen. Stay parked longer and the session ends; the next time you drive off at the track, Session 2 starts by itself, then 3, and so on (leaving the track doesn't start one). Sessions are numbered per track and per local day, and the count is kept in flash, so the first run after a power cut is the next session too. Holding the screen still ends a session at any time.

**Away from a track:** you get a menu. Drive off from it (20 km/h for 2 seconds) and Free Roam starts by itself.

- **FREE ROAM:** records your drive. A map of your route draws as you go, with elapsed time, live speed, distance and top speed. Hold the screen to finish; the summary (and the route) is saved to the SD card, unless **Record road drives** is off in [Settings](#settings). Then the drive is shown but not saved.
  Free Roam also watches for a track: see [Tracks that learn themselves](#tracks-that-learn-themselves) below.
- **NEW TRACK:** asks **Circuit** or **Course**. **Course** is for autocross, hill climbs and sprints: see [Point to point](#point-to-point-autocross-hill-climbs-sprints). **Circuit** creates a track where you are. Drive to the start/finish line and **tap the screen as you cross it**. That sets the line and starts lap 1. After your first full lap, the lap becomes the track's map, saved when you finish the session. The track is saved to `tracks.json`, so next time it's picked automatically.
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
| In the pits (parked a minute while timing): the debrief | tap: second page, then the timing screen, hold 2 s: end; driving off goes back by itself | press: the same as a tap, hold 1 s: end |
| Setting the line | tap as you cross the line, hold 2 s: end | hold 1 s: end |
| Free Roam | hold 2 s: end | hold 1 s: end |
| Performance | left: rollout / demo, centre: results, right: menu, hold 2 s: menu | press: next screen, hold 1 s: menu |
| Lap session summary | left button: menu, centre button or any other tap: Ready screen again, right button: Wi-Fi page. After a session that ended by itself, driving off starts the next | press: Ready screen again, hold 1 s: Wi-Fi page |
| Free Roam summary | tap: menu; WI-FI button: Wi-Fi page | press: menu, hold 1 s: Wi-Fi page |
| Wi-Fi | tap: Wi-Fi off (not while firmware installs) | press: Wi-Fi off |
| Settings | tap a row, drag to scroll, top bar: back (to the menu from the list) | press: next row, hold 1 s: open or flip it (on a choice page: choose it; on Brightness and Time zone: press is +, hold goes back) |
| First-boot setup | tap the choice on the left or right | press: the other choice, hold 1 s: choose it |

While timing, the screen is built to be read in one glance, top to bottom:

- **The header:** lap number and the **session clock** (time since the session started, or the time left when a [Session length](#settings) is set; yellow for the last minute, then "Time up"), battery, GPS accuracy and signal bars.
- **The band:** the big number on a full-width band, **green when you're ahead** of the reference lap and **red when you're behind**, with black digits that hold up in sun. The **Big number** setting picks what it shows: the current lap time (the default), the live diff, or the predicted lap time (the reference lap plus the diff). With the lap time or predicted lap on the band, the diff is in its top corner, sign and all. Until there's a reference lap the band is grey and shows the lap time.
- **The trend bar** beside the band: whether you've been gaining or losing over the last 3 s. It grows **up when gaining** and down when losing, full height at 0.3 s.
- **Two smaller numbers** under the band: whichever of the lap time, predicted lap and best lap aren't on it (last and best lap before there's a diff).
- **The sector strip** along the bottom: one block per corner sector, filled in as you finish each one. **Purple:** your best ever there. **Green:** your best of the day. **Yellow:** slower. Before a track has corner sectors, the last lap is shown there instead.
- **Split time:** for 5 s after each split, the split time and its gap to your best lap's split, over the strip.

Colour is never the only signal: the diff's `+` or `−` is drawn large, and the trend bar's direction says the same thing. For red-green colour blindness, the **Delta colours** [setting](#settings) swaps green and red for blue and orange, on the screen and on the LED bar.

The **live diff** is how far ahead (`−`) or behind (`+`) the reference lap you are right now, at the same point on track. It appears once there is a reference lap (see Compare with below).

At the line, **the whole screen flashes** the lap time and its gap to the reference for 4 s: purple for a new personal best (PB) at that track, green (blue) for a new best of the day, grey for any other lap. The LED bar flashes the same colour.

A tap switches between the timing and map views, moving or not. The "keep holding to end" bar only appears once a press lasts longer than a tap.

**The screen follows the situation.** Nobody should have to switch screens on track:

- **Fast:** the timing screen (or the map view, if you picked it).
- **Slow:** under 40 km/h for 5 s (`SLOW_SCREEN_KMH`, `SLOW_SCREEN_MS`: below the slowest hairpins), behind a safety car or in the pit lane, the **slow screen** shows your speed in big digits, the last lap and the session clock. When the track's map marks its pit lane (see [Track maps](#track-maps)), being in it counts as slow at any speed, and the speed turns yellow. The timing screen comes back as soon as you're over 40 km/h and out of the pit lane. The lap flash at the line still shows.
- **Parked:** the [debrief](#the-pit-debrief).

The session clock keeps running through a red flag (driving off within 10 minutes is the same session), so a countdown matches the run group.

**Best laps and PBs.** Your best clean lap ever at each track (each layout separately) is kept on the card, path and all, with its date, and so is your best lap of the day. The track list and the Ready screen show the PB and its date. The **Compare with** [setting](#settings) decides what the live diff compares with from your first lap:

- **Best ever:** your PB, until you beat it.
- **Best of the day:** your best lap today at this track, across sessions. Before you've set a lap today, your PB.
- **This session** (the default): only laps from this session, so there's no live diff until lap 2.

Whichever is picked, a quicker lap in this session takes over as the reference. Only clean laps (no [tags](#using-it)) become a PB or the best of the day. Moving the start/finish line more than 5 m (SET LINE) starts the PB again, as times at another line can't be compared.

The live diff compares your position with the path your best lap took. If you're more than 40 m off that path (`LIVE_DELTA_MAX_OFFSET_M`), for example in the pit lane, it shows `--`.

**Corner sectors.** After your first clean lap at a track, Track Rat cuts that lap into sectors, one per corner or complex: each starts at the fastest point before a braking zone, so it holds the braking, the corner and the straight after it, and a good exit counts for the corner that earned it. Braking zones closer than 3 s are joined (a chicane is one sector), and a fast sweeper you take flat out isn't one. They're numbered T1, T2 and so on from the line, saved with the track in `tracks.json` (`"sectors"`) when the session ends, and every lap after is timed through them. Your own splits stay separate, as S1, S2... Moving the start/finish line (SET LINE) makes them again from the next clean lap; to make them again otherwise, delete the track's `"sectors"` from `tracks.json`.

**Lap tags.** Each lap is checked, and a lap that can't be trusted is tagged:

- **PIT:** the car stopped (under 5 km/h) during the lap: a pit stop, a red flag, a spin.
- **SLOW:** more than 107% of the best lap: a cool-down lap, traffic, yellow flags.
- **SUSPECT:** a gap of more than 1 s between GPS fixes, a fix less accurate than ±5 m (u-blox only), or a lap more than 5% longer or shorter than the session's usual lap (a cut, once there are 3 laps to compare).

A tagged lap is never the best lap or the live-diff reference, and isn't counted in the average. The summary lists the tags beside the lap times. The slow and length checks are made again after every lap, so a short first lap that looked like the best loses that place once later laps show it was cut; the next clean lap then stands in as the live-diff reference until there's a new best.

### Point to point: autocross, hill climbs, sprints

A **stage** has a start and a separate finish (plus any splits). Each run counts as a lap: best run, live delta to it, sector colours, PB, debrief.

- **NEW TRACK, then Course**, while you're at the start (in the grid or the start box). It makes a stage called **Course 2026-10-20** (today's date: autocross courses change at every event) with its start where you are, and starts timing.
- **Standing start:** stopped within 25 m of the start, the run is **armed** (it says so on screen) and the clock starts the moment you move. If the start line has a direction and you're stopped more than 1 m behind it (timing lights a little way on), the clock starts as you cross the line instead. A start line with a direction also takes a **rolling start**: driving through it starts a run. A start with no direction (a NEW COURSE start) only does standing starts, so driving back to the grid never starts one.
- **Finish:** crossing the finish line ends the run (only the way it was set, if it has a direction). With no finish yet, **tap the screen as you cross it** on the first run: that sets the finish (with your direction, above 15 km/h) and times the run. Or don't: a run that stops for 5 s before a finish ends untimed, and you can set the finish later on the Wi-Fi page's [track editor](#track-editor-and-re-timing) (**Set finish**) and re-time the session.
- A run that stops for 5 s before the finish (a spin, a red flag) ends without a time.
- Waiting in line between runs: parked for a minute shows the debrief as usual, but the session doesn't end after 10 minutes; the whole event stays one session. A stage within 5 km at boot starts timing straight away, so the first run's standing start isn't missed. The slow screen never takes over during a run.
- In `tracks.json` a stage has `"type": "stage"` and a `"finish"` next to its `"startFinish"` (the start). Its `.vbo` says `Type: stage` and records the finish, and its `[laptiming]` has **Start** and **Finish** lines for Circuit Tools (when they have a direction).
- The thresholds are in `Config.h` (`STAGE_...`).

### Tracks that learn themselves

At a track Track Rat doesn't know, just drive. When you drive the same loop twice in Free Roam, it's a track:

- **A loop** is coming back past a point you passed at 20 km/h or more, heading the same way, within 10 m, at least 20 s and 300 m later. **Twice** means passing it a third time after a second loop about as long as the first (within 25%). One lap round the block, a drive out and back along the same road, or a hill climb and the drive back down never count.
- Track Rat then creates the track (**My Track N** in `tracks.json`), puts a provisional start/finish line where the first loop began, the way you were heading, and switches to a timing session there (**NEW TRACK: My Track N** shows on screen). It times both loops from the fixes it kept, so lap 1 and lap 2 are in the session's laps and log, and times every lap after that. The Free Roam drive up to then is saved as usual.
- If the loop is within 1.5 km of a track in the list that has no line yet, that track gets the line instead of a new one being made. If it's at a track that already has a line (Free Roam picked by hand), the session starts at that track, timed with its own line.
- The line is wherever the loop happened to begin. Move it on the Wi-Fi page's [track editor](#track-editor-and-re-timing) and re-time the session. Rename it in `tracks.json`.
- **Record road drives** off: Free Roam still watches for a loop but saves nothing; once it's a track, the session is saved like any other.
- Both loops have to fit in 12 minutes, as the fixes are kept in memory (PSRAM) to time them.
- NEW TRACK's tap-at-the-line stays, for the exact line straight away.

The rules and limits are in `Config.h` (`LEARN_...`). `TRACK_LEARNING_ENABLED 0` turns it off: the menu then waits for you to pick.

### The pit debrief

When you park, the screen answers "where's the time?":

- **Best lap, theoretical best and the gap** ("0.70 s on the table"). The theoretical best is the sum of your best time in each corner sector this session. Underneath: how many laps were used (clean laps timed through every sector; tagged laps are left out) and the theoretical best ever at this track (layout).
- **The three sectors where your best lap lost the most** against your best in that sector, marked 1, 2, 3 on the map, each with the lap the best came from and one hint from comparing the two laps' GPS speed through it: **braked 8 m early**, **4 km/h slower at the apex** (the slowest point) or **3 km/h slower on exit**, whichever cost the most. The losses in every sector add up exactly to the gap.
- **Tap** for the second page: every lap with its tag, every sector's best time with the lap it came from and what your best lap lost there, and this session against the last one here ("Best 1.04 s quicker than session 1"). Tap again for the timing screen; driving off goes back to it by itself.

The best sector times ever and of the day (for the sector strip), and the last session's best, are kept in `/tracks/<track id>/stats.json`, next to the [best laps](#session-logs). Sector times set through other gates (sectors made again after SET LINE) start afresh.

## Settings

The choices a driver makes live in **Settings**, not in the code. Change them on the device or on the Wi-Fi page. They are kept in the board's flash (NVS, apart from the program and the SD card), so they survive switching off and re-flashing.

**On the device:** tap **SETTINGS** on the menu. It is a list like the phone's Settings, in four groups (Display, Timing, GPS & Bluetooth, Recording). It is meant for the pits, before the session starts, so it is a normal touch list, not the big glove buttons of the timing screens.

- **Touch:** drag the list up and down to scroll it. Tap a switch (Screen upside down, Bluetooth, Record road drives) to flip it. Tap a row with a `>` to open its page: Speed units, Big number, Compare with, Delta colours, Session length and GPS mode show their options with a tick on the current one (tap one to choose it and go back); Brightness has a bar (tap a spot on it) and − and + buttons; Time zone has − and + for half an hour and shows the local time. **Engine data** opens the Bluetooth OBD-II adapter's screen (see [Bluetooth OBD-II adapter](#bluetooth-obd-ii-adapter-untested-on-hardware)). The top bar goes back: to the list from a page, to the menu from the list. So does **Back to menu** at the end of the list.
- **BOOT (or the big button):** press moves a highlight down the list (the list scrolls with it and starts again after the last row), hold for 1 s opens or flips that row. On a page of options, press moves down and hold chooses. On Brightness and Time zone, press is + (going round at the end) and hold goes back. A line at the bottom of the screen says so when the board has no touch screen.
- **Saved as you go:** there is no save button. A change takes effect at once, except **GPS mode**, which is used from the next restart.

| Item | Choices | What it does |
|---|---|---|
| Speed units | km/h (default), mph | Every speed on the screens, including Performance, and on the Wi-Fi page. Saved files stay in km/h |
| Screen upside down | Off (default), on | For a unit mounted upside down. Draws the screen and reads touch turned round |
| Brightness | 10% to 100% in steps of 10 (default 100%) | Dims the backlight. Auto-sleep still turns it fully off |
| Compare with | Best of the day, best ever, this session (default) | What the live diff compares with from your first lap: see [Best laps and PBs](#using-it). Used from the next session |
| Big number | Lap time (default), delta, predicted lap | The largest number on the timing screen. Delta and predicted lap show the lap time until there is a best lap |
| Session length | Off (default), 20, 25 or 30 min | With a length, the timing screens' header counts down the time left in the session instead of counting up |
| Min lap time | Auto (default), 10 s, 15 s, 30 s | Line crossings sooner than this after the last are ignored (GPS jitter at the line). Auto is 20 s, or 10 s at a kart track. Used from the next session |
| Kart everywhere | Off (default), on | On: every track is timed as a kart track (8 m lines where `tracks.json` gives no width, the kart minimum lap). Off: only tracks marked `"kind": "kart"`. **Applied at the next restart** |
| Delta colours | Green / red (default), blue / orange | Ahead and behind on the timing screen and the LED bar, and the best-of-the-day colour. Blue and orange read better with red-green colour blindness |
| GPS mode | 20 Hz, GPS + Galileo (default); 10 Hz, all systems | 10 Hz uses all four satellite systems: more satellites, half the rate. **Applied at the next restart** |
| Bluetooth | On (default), off | The RaceBox output for phone apps. Off stops it at once |
| Engine data | A screen, not a setting | Find, choose or forget a Bluetooth OBD-II adapter, and see how fast it answers. The row shows the adapter's name, Connected, or None |
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
| `IMU_ENABLED` | 1 | Look for an IMU on the I2C header at boot (see below). Harmless with none fitted |
| `CAN_ENABLED`, `CAN_TX_PIN`, `CAN_RX_PIN` | 0, 3, 14 | OBD-II over CAN (see below). Off by default |
| `CAN_BITRATE_KBPS` | 500 | The car's CAN speed: 500 for most cars since 2008, some use 250 |
| `SHIFT_LIGHT_START_RPM`, `SHIFT_LIGHT_RPM` | 5500, 6800 | Where the shift light starts filling, and where it flashes to shift. 0 = no shift light |

### CAN / OBD-II (optional, untested on hardware)

**Not yet tried on a board or a car: no transceiver has been fitted.** The OBD-II requests and replies, the polling and the shift light are tested on a PC; the CAN driver is the ESP32-S3's own (TWAI).

An SN65HVD230 CAN transceiver board (3.3 V) on IO3 (its TX/D pin) and IO14 (its RX/R pin), 3.3 V and GND, wired to the car's OBD-II socket: CAN high to pin 6, CAN low to pin 14, ground to pin 5. Set `CAN_ENABLED 1` in `Config.h` and rebuild. It's **off by default** because IO14 is also one of the GPS pins on the expansion header: with the GPS found there, CAN stays off. The serial log says when the engine answers.

- **What it reads:** engine speed, throttle position and coolant temperature, the standard OBD-II values every petrol car since about 2008 gives. Engine speed is asked for about 12 times a second, the other two about 6.
- **In the log:** every `.vbo` gets four more channels, **RPM**, **Throttle position**, **Coolant temperature** and **Intake temperature** (0 when the engine hasn't said in the last second; CAN doesn't ask for the intake, so it stays 0). Circuit Tools shows them like any other channel.
- **Coolant warning:** a red **HOT** tag in the header from `COOLANT_WARN_C` (110 °C).
- **Shift light:** from `SHIFT_LIGHT_START_RPM` the LED bar fills left to right, green, then yellow, then red; at `SHIFT_LIGHT_RPM` the whole bar flashes blue. Below the start it shows the delta as usual. The board's own LED keeps the delta. Set both for your engine.
- It only asks: it never writes to the car or clears codes. With the ignition off nothing answers, and Track Rat carries on without it.

### Bluetooth OBD-II adapter (untested on hardware)

**Not yet tried with an adapter or a car: none has been bought yet.** The ELM327 conversation is tested on a PC against recorded adapter replies; the Bluetooth side is written to NimBLE's documentation.

The same engine data as CAN, with nothing to wire: a **Bluetooth LE** OBD-II adapter plugged into the car's OBD-II socket. It must be a Bluetooth LE model, as the ESP32-S3 has no Classic Bluetooth: the Veepeak OBDCheck BLE+ (about $30) or the OBDLink CX (faster, CAN cars only). The Veepeak Mini, Wi-Fi adapters and the OBDLink MX+ can't connect.

- **Choosing it:** plug the adapter in and switch the ignition on. In **Settings → Engine data**, tap **Find adapter**. A 10-second scan lists what it finds by name and signal strength (names with OBD, OBDII, VEEPEAK, OBDLink, vLink or IOS-Vlink, and anything offering the FFF0 or FFE0 service). Tap yours. Track Rat remembers it and connects by itself whenever it's in reach, trying every 5 to 10 seconds. **Forget** disconnects and forgets it. With no adapter chosen, nothing happens at all.
- **In the header:** a blue **OBD** tag while it's connected, beside **BT**.
- **What it reads:** engine speed, throttle, coolant and intake air temperature, whichever the car supports (it asks the car first). Cars that only report the pedal give that as the throttle. Engine speed is asked for most, then throttle; the temperatures once a second. Cheap adapters manage 6–15 requests a second; with the two speed-ups below an ELM327 reaches 20–40 on a CAN car, so engine speed about 10 Hz and throttle about 7 at 20 a second. Each value is stamped at the middle of its request.
- **Speed-ups, tried once per connection:** the "one reply" digit (`010C1`: the adapter answers as soon as the engine has, instead of waiting out its timeout) and engine speed and throttle in one request (`010C11`). Clones that answer `?` simply go without.
- **The Engine data screen** shows the adapter, what it's doing (setting up, finding the car, reading the engine, or no answer from the car with the ignition off), the car's protocol, requests a second, and each channel's rate.
- **Same as CAN from there:** the `.vbo` gets the four engine channels from the moment an adapter has been chosen, and the shift light and the HOT tag work the same. With both fitted, CAN wins.
- **Alongside the phone:** the RaceBox output keeps advertising and working while the adapter is connected. Up to four Bluetooth connections at once (the phone, the adapter, and room for the camera).
- Like CAN, it only asks: it never writes to the car or clears codes. `OBD_BLE_ENABLED 0` in `Config.h` leaves it out.

### IMU (optional, untested on hardware)

**Not yet tried on a board: no IMU has been fitted.** The maths is tested on a PC; the I2C driver is written to the datasheet.

An LSM6DSO-class IMU (LSM6DSO, DSOX, DSR, DSL, DS3, DS3TR-C: a breakout from Adafruit, SparkFun or Pololu) on the board's I2C header, alongside the touch controller: 3.3 V, GND, SDA (IO16) and SCL (IO15). It's found at boot at address 0x6A or 0x6B (the touch controller is at 0x38); the serial log says which. With none fitted, nothing changes.

- **Mounted any way round:** it learns how at every boot. **Up** from two seconds of standing still, **forward** from speeding up and braking in a straight line (about 8 g of it added up: a few seconds of driving). Until then the g-forces still come from the GPS.
- **Then** the `.vbo` log's lateral and longitudinal g are the IMU's (+ = turning right, + = speeding up, as before), and the RaceBox Bluetooth packet carries its g (x forward, y right, z up, in milli-g) and rotation rates (roll, pitch, yaw) instead of a device at rest. Real g is quicker and smoother than g from the GPS, which lags by a fix or two.
- Read at 104 Hz and averaged between GPS fixes; ±4 g and ±500 °/s.

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
- **`radius`:** about half the track width plus a few metres. 10–20 m is typical. Without one, a line is 15 m either side, or 8 m on a kart track.
- **`"kind": "kart"`** (on the venue or a layout): a kart track. Lines with no `radius` are 8 m either side, and the minimum lap is 10 s (the **Min lap time** setting can change it). `tools/fetch_canada_tracks.py` marks karting tracks from OpenStreetMap; the **Kart everywhere** setting treats every track as one.
- **Placement:** put splits where no other part of the track passes within `radius`, or give them a `heading`.
- **`splits`:** optional.
- **`sectors`:** corner sectors, the same form as `splits`, made by the timer from your first clean lap (see [Using it](#using-it)). Each is the gate at the end of one sector: the first ends T1. You can move or delete them by hand; delete them all to have them made again.
- **No start/finish yet:** a track can have a `"center": {"lat": .., "lon": ..}` instead of `startFinish`. It's still found automatically, and the timer asks you to tap at the line the first time.
- **Saved by the timer:** new tracks, start/finish lines marked on track (with their direction), and maps and corner sectors from your first lap are written back into `tracks.json` on the SD card (maps when the session ends). It's saved compactly, one track per line.

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
- **Kart tracks:** karting venues (OSM `sport=karting`), and venues whose every raceway is a kart track, get `"kind": "kart"`. A car circuit with a kart track beside it stays a car circuit.
- **Your existing tracks are never changed,** except that a karting track you already have gets `"kind": "kart"` if it has no `kind`. That includes any track within 1 km of one you already have.

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

- **Sessions:** every file in `/sessions`, by day and track, newest first. Tap a file to download it. Files from the same session (`.json`, `.vbo` and so on) are shown together. Times are in your phone's time zone. Each session has its logbook note under it: see [Logbook](#logbook) below.
- **Tracks:** download the `tracks.json` Track Rat is using, or upload an edited one. The old one is kept as `tracks.json.bak`, and Track Rat restarts to load the new list. If power is cut mid-save (here or on the device) and `tracks.json` is gone, the next boot puts back the newest complete copy it finds (`.tmp`, `.upload`, then `.bak`).
- **Track editor** (on the Tracks tab): see [Track editor and re-timing](#track-editor-and-re-timing) below.
- **Compare:** any two laps at a track, from any sessions: speed against distance, the time gap against distance, and the map coloured by who was faster where. Plus the season: your best lap at each visit and your PB over time. See [Lap comparison](#lap-comparison) below.
- **Firmware:** install a new `firmware.bin` (from `.pio/build/es3c28p/` after `pio run`). The page and Track Rat both check the file first, as the SD card update does: it must be an ESP32-S3 app image that fits the slot, so a `bootloader.bin` or a file for another chip is refused before anything is written. A progress bar shows while it's sent; then Track Rat restarts with Wi-Fi off. Keep it powered until it does.
- **Device:** firmware version, free space on the card and in flash, GPS status and memory.
- **Settings:** the same items as the Settings screen on Track Rat; see [Settings](#settings). Changing one asks for the upload code and takes effect at once, except the GPS mode, which waits for a restart (the page offers one). The time zone is shown but not changed here.

Each time the page opens it tells Track Rat your phone's time zone, which becomes the **Time zone** setting. From then on, session files are named in your local time instead of UTC. Daylight saving time changes the zone, so it's correct again the next time a phone opens the page. You can also set it on the Settings screen, but the next phone to open the page replaces it.

Good to know:

- **Wi-Fi is off while timing.** It's only on while the Wi-Fi screen shows, and starting a session turns it off. Bluetooth (RaceBox) keeps working while it's on.
- **Downloads in the sign-in window:** the window phones open by themselves often can't save files. If a download doesn't start, open http://192.168.4.1 in Safari or Chrome while still on the TrackRat network.
- **Mobile data:** some phones keep using mobile data for everything when a Wi-Fi network has no internet. If the page won't load, tell the phone to stay connected or turn mobile data off.
- **Upload code:** installing firmware, replacing `tracks.json` (or saving the track editor's lines), re-timing, saving a note or changing a setting asks for the 4-digit code on Track Rat's Wi-Fi screen, so only someone who can see the screen can do it. After 5 wrong codes, uploads are locked until Wi-Fi is turned off and on again (which also gives a new code). Downloads don't need it.
- **Anyone nearby can join** and download sessions while it's on, so turn it off when you're done.

### Track editor and re-timing

Record first, decide later: a wrong start/finish, a missing split or a layout Track Rat didn't know never costs a session, because every fix is in the log.

1. On the **Tracks** tab, pick a session (any `.vbo` log on the SD card). Its laps are drawn from the log on your phone, no map download needed: every lap in grey, one in full, with the track's start/finish (green, with an arrow the way the track runs), splits (blue, S1, S2...) and corners (purple, T1, T2... or their names) on top. The track is the one the log names; pick another from the list if it was timed at the wrong one.
2. Edit:
   - **Move line:** tap the path where the start/finish should be. Its direction is taken from the path there. The corner sectors go with the old line, as with SET LINE on the device: Track Rat makes them again from the next clean lap.
   - **Set finish** (stages only): tap the path where the finish is. Its direction is taken from the path there.
   - **Flip direction:** turns the line round.
   - **Add split / Remove split:** tap the path to add one (numbered in the order a lap passes them, from the line), or tap a split to remove it.
   - **Corners:** type a name for any corner sector ("The Wall"); clear it to go back to the number. Names are saved in `tracks.json` (`"name"` on each of `"sectors"`).
   - **New layout:** makes a layout of the venue (for example a short course), starting from this track's line, splits and map. A venue with no layouts keeps what it had as a layout called **Main**, as `tools/fetch_track_map.py` does.
3. **Save lines:** asks for the upload code and sends the edited `tracks.json`, the same way as uploading one: the old file is kept as `tracks.json.bak` and Track Rat restarts to load it.
4. Turn Wi-Fi on again and open the editor: **Re-time** times **this session** or **every session at this track** again from their logs, with the lines Track Rat has now. It runs in the background on the device (the page shows how far it is, and the Wi-Fi screen stays on until it's done), rewrites each session's summary (laps, splits, sectors, tags), and puts the PB and the best of the day right: the quickest clean lap replaces a stored one that was timed at another line, or that it beats. A session needs its `.vbo` log on the SD card to be re-timed.

### Lap comparison

On the **Compare** tab, pick a track, then lap **A** (blue) and lap **B** (orange): each from any session at that track with a `.vbo` log on the SD card. The lap lists come from the session summaries, with each lap's time and tags; the best lap is picked to start with. Your phone draws everything from the logs (Track Rat only serves the files):

- **Speed against distance:** both laps on one chart, so you see where one brakes later or carries more speed.
- **Time gap against distance:** B's time minus A's at the same point on track. Going up means B is losing time there. B's lap is stretched to A's length, as two laps never measure quite the same distance, so the same corner lines up. The last point is the gap at the line.
- **Who was faster where:** lap A's path, each stretch blue where A was gaining and orange where B was.
- **Season:** the best lap of each visit (day) at the track, newest first, with your PB as it stood then, and a chart of both over time.

Laps are cut where they were cut live (the log's Lap column). After [re-timing](#track-editor-and-re-timing) with a moved line, the lap list follows the new summary but the traces still follow the old cuts.

### Logbook

Keep a note with each session: tyre pressures, the weather, setup changes, what your instructor said.

1. On the **Sessions** tab, tap **Add note** under a session (or **Edit note** to change one).
2. Type it and tap **Save note**. It asks for the upload code. Up to 1000 bytes (about 1000 letters, fewer with accents or emoji).
3. To delete a note, clear it and save.

The note is saved next to the session as `<date>_<time>_<track id>_note.json` (`{"note": "..."}`), on the SD card or in flash, wherever the session is. When you download the session's `.vbo` from the page, the note is in its `[comments]` as `Note: ...` lines, so Circuit Tools shows it too. The file on the card is not changed, so a `.vbo` copied straight off the card has no note.

**Voice notes** (hold on the debrief to record 30 s with the board's microphone) aren't built yet: the microphone's codec (ES8311) has to be brought up on a board first. `VOICE_NOTES_ENABLED` in `Config.h` stays 0 until then.

### Changing the page

The page is a single file, `web/index.html` (plain HTML and JavaScript, no libraries), plus its fonts in `web/fonts/`. `tools/build_web.py` gzips the page into `data/web/index.html.gz` and copies the fonts to `data/web/fonts/` every time PlatformIO runs; it also puts the git version on the Device tab. To put a new page on the board:

```bash
pio run -e es3c28p -t uploadfs
```

Without it, the board shows a plain page with the session list, `tracks.json` and firmware upload. There is no Settings tab then; use the Settings screen on the board.

## Session logs

Sessions are saved in `/sessions` on the SD card, as two files with the same name. For a venue with layouts, the track id includes the layout (`calabogie.full`) and the summary also names the layout.

- **`<date>_<time>_<track id>.vbo`:** every GPS fix, up to 20 a second (10 in the 10 Hz GPS mode): UTC time, position, speed, heading, height, satellites, HDOP (GPS precision), lap number, lateral and longitudinal g worked out from the GPS (+ = turning right, + = speeding up), and a lap tag mark. About 6.8 MB per hour of driving. The **Lap tag** channel marks the fixes that tag a lap where they happened: 1 = stopped, 4 = after a gap of more than 1 s, 8 = accuracy worse than ±5 m (added together when there are several). The header names the session number (`Session: 2`).
- **`<date>_<time>_<track id>.json`:** the summary: the session number (`"session"`), every lap and split time, each lap's corner sector times (`"sectorsMs"`: the time into the lap at the end of T1, T2... the last sector ends at the line), max and average speed and distance per lap, each lap's tags (`"tags": ["pit", "slow", "suspect"]`, and for a suspect lap why: `"suspect": ["gps gap", "accuracy", "distance"]`), the best clean lap, the average of the untagged laps, and the name of its `.vbo`.

File names use UTC until the **Time zone** setting is set, either on the Settings screen or by a phone opening the Wi-Fi page. Then they use your local time. Times inside the `.vbo` are always UTC. Speeds in the files are always km/h, whatever the Speed units setting.

Free Roam drives are saved the same way, as `<date>_<time>_freeroam.vbo` and `.json`, unless **Record road drives** is off in [Settings](#settings): then nothing is saved. The summary holds the duration, distance, and top and average speed.

**Best laps** are kept in `/tracks/<track id>/` on the card: `best.bin` (the PB) and `day.bin` (the best of its day), each about 30 KB: the lap's path (position, time and speed every fix), its time, its date and where the start/finish was. They're saved the same safe way as the summary.

**Without an SD card,** only the summaries and best laps are kept, in flash (a full log fills it in about half an hour). Free Roam then saves its route as GPS points about 10 m apart in the summary. The log at boot says when there's no card.

### Opening a session in Circuit Tools

[Circuit Tools](https://vboxmotorsport.co.uk/us/circuit-tools) is Racelogic's free analysis software for Windows, macOS and iOS. It shows lap overlays and speed and time-gap graphs.

1. Copy the `.vbo` from the card's `sessions` folder to your computer.
2. Open it in Circuit Tools.
3. **If the track has a map** in `tracks.json`, the file includes the start/finish line and any splits, so the laps appear straight away. Corner sectors come after the splits, named T1, T2... **Otherwise** (new tracks, a line you marked during the session), set the start/finish line in Circuit Tools. Everything you drove is in the file either way.

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

`test/test_settings.cpp` checks the settings rules (defaults, limits, stepping, units, big number, predicted lap, brightness, time zone, min lap time and kart everywhere), the Settings list (where its rows are, what a tap hits, scrolling, the pages, touch and BOOT) and the first-boot questions, the field names the Wi-Fi page uses, and a simulated first boot followed by a lap session:

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

`test/test_elm327.cpp` checks the Bluetooth adapter's ELM327 conversation against a simulated adapter: the set-up commands in order, the supported-PID questions, a clone that rejects the "1" suffix or multi-PID requests, `SEARCHING...` before a slow first reply, spaces left on, replies cut into 1, 7 and 20-byte notifications, a missing prompt (a timeout, then ATZ after three), the engine stopping answering and coming back, the pedal as throttle, values stamped at the middle of their request, reading `NO DATA`, `?`, `STOPPED`, bus errors, multi-ECU, multi-PID and multi-frame replies, which scanned devices are listed, and the schedule's rates at 10, 20 and 40 replies a second:

```bash
g++ -std=c++17 -Iinclude test/test_elm327.cpp -o /tmp/elm && /tmp/elm
```

`test/test_obd.cpp` checks OBD-II over CAN: the requests, reading engine speed, throttle and coolant temperature from the engine's replies while ignoring everything else on the bus (other messages, other modes, "not supported", short and multi-frame replies), values going stale, the polling schedule (engine speed every other request, one at a time, a gap between, a timeout), and the shift light. `test/test_vbo.cpp` checks the log's engine channels:

```bash
g++ -std=c++17 -Iinclude test/test_obd.cpp -o /tmp/obd && /tmp/obd
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
           TimingScreen.h (the timing screen's colours, trend bar, sector strip and ghost),
           SituationScreen.h (the slow screen, the pit lane, the session clock),
           Retime.h (which sessions are re-timed, when best laps are replaced),
           SettingsData.h (the settings and their rules), SettingsFlow.h (how keys move through
           the Settings and first-boot screens), Obd.h (OBD-II values and schedules),
           Elm327.h (the Bluetooth adapter's conversation), EngineScreen.h (Settings > Engine data), headers
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
           CanObd (OBD-II over CAN), ObdBle (the Bluetooth OBD-II adapter), EngineData (either)
           WifiPortal (Wi-Fi page)
include/fonts/  the screen fonts (Inter), made by tools/make_fonts.py
web/       the Wi-Fi page and its fonts (copied into data/web when building)
data/      tracks.json
test/      host-side tests (check.h: shared by all of them)
```
