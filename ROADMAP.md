# Track Rat feature plan

What to build next. It's based on what this hardware can and can't do, how people use lap timers at real events, and what the commercial timers (AiM, Garmin, VBOX, Apex Pro, RaceBox) and phone apps (RaceChrono, Harry's) get right and wrong.

**The idea in one line:** plug it in and drive. Track Rat records everything, times what it can, and lets you fix anything afterwards, so nothing ever needs setting up before you go out.

## The short version

The features worth building, roughly in order:

1. **Log every GPS fix** to the SD card, in a format the free analysis tools open (VBO). Today only lap times are kept and the path is thrown away.
2. **Automatic sessions.** Notices when you're in the pits, splits the day into sessions, and tags in-laps and bad laps. No START or END to press.
3. **Settings on the device:** units, upside-down mounting, brightness, what to compare against. Today they need a recompile.
4. **Direction-aware lines and track layouts**, so a nearby straight or the pit lane can't trigger a lap, and each layout keeps its own records.
5. **Your best-ever lap at each track**, used for the live delta from the first lap of the day and shown as your PB.
6. **Automatic corner sectors and a pit debrief:** best lap, theoretical best, and the three corners where the time went, on the map, as soon as you park.
7. **A delta-first timing screen** built for a dim screen in sunlight: colour you can read from the corner of your eye.
8. **A Wi-Fi page on your phone:** download sessions, change settings, update firmware, move the start/finish line and re-time old sessions. No app.
9. **Unknown tracks learn themselves:** drive the same loop twice and it's timed.
10. **More ways to drive:** autocross and hillclimbs (separate start and finish), and acceleration and braking runs (0–100 km/h, ¼ mile, 100–0).

Plus two cheap, optional add-ons: a big glove-friendly button and an LED delta bar.

## What the hardware allows

| Hardware | What it means for features |
|---|---|
| 2.8" 320×240 screen, about 230 nits measured | Hard to read in direct sun (sunlight-readable displays are around 1,000 nits). Use big colour blocks and few numbers. An LED bar helps in open cars and karts. |
| Capacitive touch | Doesn't work with racing gloves, and vibration and rain cause false taps. Nothing on track may need a touch. In the pits, use screen thirds or a button. |
| BOOT button | The only physical input, and it's on the board, so hard to reach in a case. A button on a spare pin fixes this. |
| ESP32-S3 Wi-Fi and Bluetooth LE | A web page on your phone can replace an app. Bluetooth already streams to phone apps as a RaceBox Mini. There's no Classic Bluetooth, so no audio to headsets or car stereos. |
| 240 MHz dual core, 8 MB PSRAM | Analysing a whole session on the device is easy. |
| 16 MB flash: two 6.25 MB app slots, 3.4 MB LittleFS | Firmware is 0.8 MB, so over-the-air updates fit, and so do the web page's files. |
| microSD | Every fix at 20 Hz is about 7 MB per hour of driving. A card holds years of it. |
| USB power from the car | Cuts out when the ignition goes off, at any moment. Saving must survive that. |
| Battery socket and charger, battery voltage on IO9 | Can run on a kart, keep going when the car is switched off, and show a battery gauge. |
| Speaker amp (1.5 W) and microphone | Too quiet to hear under a helmet. The microphone is useful for voice notes in the pits. |
| RGB LED (IO42); free pins IO2, IO3, IO14, IO21; I2C header shared with touch | Room for an LED bar, a button, an IMU or a CAN (OBD-II) adapter. |
| u-blox M10 GPS: 20 Hz with GPS and Galileo, about 1.5 m accuracy | Excellent for lap times and speed. Not accurate enough to coach the racing line. |
| No IMU | g-forces come from the GPS. Fine for analysis, slightly delayed live. |

## Who uses it, and how

- **Track day driver (the main user).** Four to six sessions of 20–30 minutes in a day, with an hour or so in the paddock between them. Wants lap times and a live delta while driving, and an answer to "where can I find time?" before the next session. Comes back to the same few tracks, so wants to see progress over a season. Common complaints about other tools: forgetting to press start, phones overheating, in-laps spoiling the results, and setting up tracks.
- **Karting.** Short laps, lots of sessions, no power on the kart, mounted on the steering wheel in full sun with heavy vibration. Many kart tracks run several layouts and both directions.
- **Autocross and hillclimb.** Separate start and finish, standing starts, three to six runs, and a new course at every event. GPS won't match the official timing exactly; the value is comparing your own runs and seeing where you lost time.
- **Road drives and enthusiasts.** Free Roam, plus acceleration and braking runs, which are what the RaceBox and Dragy are mostly bought for.
- **Data and video people.** Want the raw data in tools they already use (Circuit Tools, RaceChrono, Telemetry Overlay), lined up with GoPro footage.

## A day at the track, as it should feel

- **07:40, paddock.** Plug in. The logo plays, the GPS locks, and the screen says "Calabogie, your best 2:21.4 (3 June)". Nothing to press.
- **09:00, session 1.** Out lap; timing starts at the line. Until you set a lap today, the delta compares with your best ever. At each crossing the whole screen flashes the lap time: purple for a new PB, green for best of the day.
- **09:20, checkered flag.** In-lap, park. After a minute the screen shows: best 2:22.9, theoretical best 2:21.8, and "most time lost: T5 0.4 s, T12 0.3 s, T1 0.2 s", marked on the map. You've read it before you've got your helmet off.
- **10:40, session 2.** The car was off in between. Track Rat starts session 2 by itself and compares with your best of the day.
- **Lunch.** Your phone joins the TrackRat Wi-Fi. Compare your best lap with your PB lap (speed and time-gap graphs) and add a note: "32/30 psi hot, pushing in T5".
- **Home.** Download the day as VBO for Circuit Tools, or line it up with GoPro footage in an overlay tool.

Touches needed all day: none.

## Design rules

1. **No touches on track.** Track, session, laps, pits and saving are all automatic. Gloves, vibration and the driver's attention all rule out touch while driving.
2. **Record first, decide later.** Everything is logged, so a wrong start/finish line, a missing split or an unknown track never costs a session. Fix it afterwards and re-time.
3. **The screen follows the situation.** Fast: timing. Slow: speed. Parked: the debrief. Nobody should need to switch screens by hand. (VBOX does a version of this, switching to speed in the pit lane and behind a safety car.)
4. **One glance, one answer.** Colour first, then one big number. Everything else waits for the pits.
5. **Never lose data.** The ignition can go off at any moment.
6. **Hand off to tools people already have.** Don't build a full analysis suite for a 2.8" screen. Build the three answers that matter in the paddock and export the rest.

## Features

Sizes are relative: S small, M medium, L large.

### Phase 1: Never lose a lap

**1.1 Full-rate session log (M)**

- Every fix, 20 a second: UTC time, position, speed, heading, height, satellites and accuracy, plus lateral and longitudinal g worked out from the GPS. Lap and sector events are marked.
- Written as Racelogic **VBO**, a plain text format. The free Circuit Tools (Windows, macOS and iOS) opens it directly, with lap overlays, time-gap and speed graphs. Include the `[laptiming]` section so laps and splits appear without any setup. Check that a sample file opens before settling the details.
- The session JSON stays as the lap summary and points to its log.
- Free Roam uses the same log instead of a point every 10 m.
- Buffer in PSRAM and write once a second from a task on the other core, so a slow SD write never holds up timing. (The fix queue already rides out about 3 s.)
- Without an SD card, flash (3.4 MB) only fits about half an hour of logs. Keep summaries and best laps there, and say at boot that full logs need a card.
- **Why:** almost every feature below needs the path. One Garmin Catalyst reviewer complained that its raw data can't be exported.

**1.2 Safe saving (S)**

- The summary is rewritten after every lap. If the power goes mid-write, the file is corrupt. Write to a temporary file, then rename it.
- The log only ever grows, so a power cut loses at most the last second.
- At boot, finish any session that wasn't closed: rebuild its summary from its log.

**1.3 Automatic sessions and lap tags (M)**

- **Sessions.** Parked (under 5 km/h) for a minute means you're in the pits: show the debrief (2.3). Move off within 10 minutes (red flag, a quick check) and the same session carries on. After longer, or after the car has been switched off, the next run is Session 2, 3, and so on, of the same day at that track. Holding the screen to end a session still works.
- **Lap tags.** Each lap gets a tag, shown in lists and saved in the log:
  - *Pit lap:* includes a stop.
  - *Slow lap:* more than 107% of the best (cool-down, traffic, yellow flags). Listed, but left out of averages.
  - *Suspect:* a GPS gap over 1 s, poor accuracy, or a lap distance more than 5% off the usual. Never becomes a best lap or a reference.
- **Why:** a bad lap must never become your reference or your PB. One Catalyst owner found that short in-laps confused its optimal lap.

**Status: done** (2026-10-20, commit bda0d37)

**1.4 Settings on the device (M)**

- Stored in flash (Preferences) and changed on a big-button settings screen, or later on the Wi-Fi page. Today they're `#define`s in `Config.h`; pins stay there.
- Speed units; screen upside down (for mounting); brightness (PWM on the backlight pin, IO45); compare against: best of the day, best ever, or this session; big number: delta, predicted lap or lap time; GPS mode: 20 Hz with GPS and Galileo, or 10 Hz with all four systems for tracks lined with trees; Bluetooth on or off; Wi-Fi in the pits on or off; time zone; record road drives on or off.
- The first boot asks only two things: which way up, and km/h or mph.

**1.5 Direction-aware lines and layouts (M)**

- Store the direction of travel with every line, taken from the tap that set it or from the map. A crossing the wrong way, such as the pit lane or a straight running alongside, is ignored. This matters most on compact kart tracks, where two straights can both pass within the gate's 15 m either side of the line.
- A venue can have several layouts (Full, Short, Reverse, the kart track next door). Each has its own line, sectors, map and PBs, and PBs never mix between layouts. After one lap, Track Rat checks which layout's map the lap matches, and switches if it picked the wrong one.
- This comes before PBs (2.1) so that PBs are stored per layout from the start.

**1.6 Battery (S)**

- Battery percentage in the header when a battery is fitted (voltage on IO9).
- Parked for 20 minutes with no input: screen off, then sleep. BOOT or a tap wakes it. This stops a battery-fitted unit from flattening itself in a parked car.
- Low battery: save, say so, and sleep cleanly.
- Measure the real current draw before choosing a battery size.

**1.7 Binary GPS messages (M)**

- Ask the M10 for UBX-NAV-PVT instead of the GGA and RMC text sentences: one packet per fix with millisecond time, position, 3D velocity, and real accuracy figures (± metres, ± m/s).
- Shows GPS quality as "±1.2 m" instead of a satellite count, feeds the suspect-lap check, fills the RaceBox packet's accuracy fields properly (today they're estimated from HDOP), and gives vertical speed for slope checks (4.3).
- Keep reading NMEA for GPS modules that aren't u-blox.

**1.8 Firmware update from the SD card (S)**

- Put `trackrat.bin` on the card. At boot, Track Rat offers to install it into the second app slot, which already exists. People can update without PlatformIO.

### Phase 2: Know where the time is

**2.1 Best-ever lap and PBs (S)**

- Keep the best clean lap's path for each track layout on the card, about 30 KB.
- The live delta works from your first lap of the day. You're compared with your best ever until you set a lap today, then with your best of the day (a setting). Today a session has no delta until lap 2.
- Your PB and its date show on arrival and in the track list. Beating it flashes the screen purple.
- **Why:** AiM and VBOX both let you load a reference lap, and people return to the same tracks again and again.

**Status: done** (2026-10-20, commit 7b31515)

**2.2 Automatic corner sectors (M)**

- After the first clean lap at a track, split the lap at the fastest point before each braking zone, and merge any tiny pieces. A road course usually ends up with 8–14 sectors, about one per corner or complex, numbered T1, T2 and so on from the line.
- Each sector covers a braking zone, the corner and the straight after it, so a good exit counts for the corner that earned it. Where the cuts fall matters: one reviewer found a stitched "optimal lap" that joined pieces no car could drive in sequence (a gear change that never happened).
- Saved with the track like splits. That keeps them the same across sessions, lets you edit them on the Wi-Fi page, and puts them in the VBO so Circuit Tools shows the same sectors. Your own splits stay as S1, S2, S3.

**Status: done** (2026-10-20, commit 1ab4e1f)

**2.3 Theoretical best and the debrief (M)**

- **Theoretical best:** the sum of your best time in each sector, for this session and for all time.
- **The debrief** appears when you park: best lap, theoretical best, the gap between them ("0.7 s on the table"), and the three sectors where your best lap lost most against your best in that sector. They're marked on the map, with the lap each best came from: "T5 0.31 s, best on lap 7". All the sector gaps add up to the 0.7 s, so the numbers explain each other.
- Each of the three gets one hint, from comparing the two laps' speed: "braked 8 m earlier", "4 km/h slower at the slowest point" or "3 km/h slower at the exit". They lean on GPS speed, which is far more accurate than GPS position, so they hold up where line comparisons wouldn't.
- Tap through to: every lap with its tag, every sector, and this session against the last ("best 1.0 s quicker than session 1").
- Only clean laps count, and the screen says which laps were used.
- **Why:** the Garmin Catalyst's three improvement points straight after a session are among its best-reviewed features: one owner read them in the paddock without a laptop. Reviewers also wanted to see which laps fed the result, and in-laps left out. AiM added a theoretical-best mode to the Solo 2 DL.

**Status: done** (2026-10-20, commit e0d33ab)

**2.4 A delta-first timing screen (M)**

- **Layout, top to bottom:** a header with lap number, session clock, GPS accuracy and battery; the delta in the largest digits on a full-width band, bright green when ahead and red when behind, with black digits that hold up in sun; the current lap time and predicted lap, smaller; a strip of sector blocks. The running lap time is shown full size at the line anyway.
- **Trend bar** at one edge: gaining or losing over the last few seconds. VBOX shows a delta bar, and Apex Pro's second generation added the same idea to its LEDs.
- **Sector strip:** each block fills in as you finish that sector. Purple: best ever. Green: best of the day. Yellow: slower.
- **At the line:** the whole screen flashes the lap time and the gap. Purple for a PB, green for best of the day.
- **Don't rely on colour alone.** About 1 in 12 men has red-green colour blindness. Draw the + or − sign large, make the trend bar grow up for gaining and down for losing, and offer a blue and orange palette.
- **Options:** the big number can be the predicted lap time (reference lap plus delta), as on AiM's units and now in RaceChrono, or the classic running lap time.
- **Map view** stays as the second screen, with a "ghost" dot showing where your reference lap was at this moment.
- **Taps switch the view,** moving or not. Ignoring quick taps while moving (for bumps and rain) was tried and dropped: in practice a tap didn't work on track.
- Test outdoors at full brightness. Colour blocks in sunlight are the real test of this screen.

**Status: done** (2026-10-20, commit 3f3c821)

**2.5 Screens that follow the situation (S)**

- **Slow** (under 40 km/h for 5 s, or in the pit lane when `tracks.json` marks it; the OpenStreetMap scripts already know which ways are pit lanes): big speed, last lap, session clock. The timing screen comes back when you speed up. The threshold needs to sit below the slowest hairpins.
- **Parked:** the debrief (2.3), with the local time, since track days run to a schedule.
- **Session clock** in the header: time since you left the pits. Run groups are usually 20–30 minutes, and drivers want to know how long is left. Optionally a countdown, if a session length is set.

**Status: done** (2026-10-20, commit 5ba16e2)

### Phase 3: Your data on your phone

**3.1 The Wi-Fi page (M)**

- When parked, Track Rat can open a network called TrackRat-1234. Join it and the page opens by itself, like a hotel login page (or go to 192.168.4.1). No internet and no app needed. Wi-Fi stays off while timing.
- Sessions by day and track; downloads as VBO, CSV, GPX (for Free Roam drives) or JSON; settings; the track list; firmware updates.
- The page sets the device's time zone from the phone, so file names and days use local time instead of UTC.
- The page's own files are gzipped in LittleFS; sessions are served straight from the card.
- **Why:** MyChron 6 listings make a point of downloading over Wi-Fi with no PC. A web page also avoids writing and maintaining phone apps.

**3.2 Track editor and re-timing (L)**

- Draws your recorded laps (no map download needed), with the line and sectors on top. Drag the line, set its direction, add or remove splits, name corners, or split a venue into layouts.
- **Re-time:** Track Rat replays the saved log through the same `LapTimer` code and rewrites the lap times and PBs, for one session or every session at that track.
- This is what makes "record first, decide later" true.

**Status: done** (2026-10-20, commit dac470d)

**3.3 Lap comparison (L)**

- Pick any two laps at a track, from any sessions, and see speed against distance, time gap against distance, and the map coloured by who was faster where. These are the views every analysis tool leads with.
- The phone draws the graphs from the log; the ESP32 only serves the file.
- Season view: best lap at each visit, and your PB over time.

**Status: done** (2026-10-20, commit ae3a8b0)

**3.4 Logbook (S)**

- A note for each session (pressures, weather, setup, your instructor's advice), typed on the phone and saved with the session and its exports.
- Later: hold the button on the debrief screen to record a 30 s voice note with the board's microphone, played back on the Wi-Fi page. The thought is freshest in the car.

**Status: done** (2026-10-20, commit 1b8cf77) for text notes. Voice notes wait behind `VOICE_NOTES_ENABLED` (0) in `Config.h` until the ES8311 codec is brought up on a board.

### Phase 4: More ways to drive

**4.1 Unknown tracks learn themselves (M)**

- Today, away from known tracks you get a menu, and if you just drive off, nothing is saved.
- Instead, start recording when you move off. When you drive the same loop twice (back past a point you passed at speed, heading the same way, at least 20 s and 300 m later), it's a track. Track Rat creates it, puts a provisional line where the loop began, times both laps from the log, and times every lap after that. Move the line later (3.2) and re-time. Needing two loops keeps a drive round the block from becoming a track.
- AiM's Solo 2 does something similar ("auto-learning") for tracks that aren't in its database.
- NEW TRACK's tap-at-the-line stays, for anyone who wants the exact line straight away.
- With road drive recording off, Track Rat still watches for a loop but doesn't save the drive.

**Status: done** (2026-10-20, commit 7e202a6)

**4.2 Point to point: autocross, hillclimbs, sprints (M)**

- A track type with separate start and finish lines, plus splits.
- **Standing start:** armed while stopped near the start. The clock starts at the first movement, or at a start line a few metres on (timing lights usually sit a little past the line).
- **Finish:** crossing the finish line. If none is set yet, tap as you cross; or the run ends when you stop, and you set the finish afterwards and re-time.
- Each run counts as a lap: best run, live delta to it, sector colours.
- **NEW COURSE** makes a stage named with the date, since autocross courses change at every event.

**Status: done** (2026-10-20, commit b3f0b2f). NEW COURSE is **Course** on the NEW TRACK screen (the menu has no room for a seventh button).

**4.3 Acceleration and braking runs (M)**

- A mode showing big speed, armed while stopped: 0–100 km/h (or 0–60 mph), 100–200 km/h, ⅛ and ¼ mile with trap speed, and 100–0 braking distance. Starts on movement, with an optional 1-foot rollout (the US drag-strip convention). A result card for each run, and a history.
- Warns when the run wasn't level, from the change in height. RaceBox corrects for slope.
- **Why:** acceleration runs are what the RaceBox and Dragy are mostly sold for, and this needs no new hardware.

**4.4 Karting (S)**

- Mostly covered already: battery (1.6), directional lines (1.5). Add a smaller default gate (8 m) and the minimum lap time as a setting.

**Status: done** (2026-10-20, commit 0d30dc5). `"kind": "kart"` in tracks.json (marked from OSM by `fetch_canada_tracks.py`; the bundled `data/tracks.json` gets the marks the next time the script runs), or the **Kart everywhere** setting; **Min lap time** setting (Auto, 10, 15, 30 s).

### Phase 5: Optional add-ons

All of these use the spare pins on the expansion header, with the GPS on the UART connector. (With the GPS on the expansion header instead, only IO2 and IO3 are free.)

| Add-on | Parts | What it adds |
|---|---|---|
| **Big button** | A 25–30 mm push button on IO21 | Does what BOOT does, mounted on the dash or wheel. Works with gloves. |
| **LED delta bar** | 8–12 WS2812B LEDs on IO2 | Delta in peripheral vision: lights grow right in green when ahead, left in red when behind; purple flash for a PB. AiM (10 LEDs), VBOX (6) and Apex Pro all use LEDs for this. The best fix for the dim screen. A first step: show the delta colour on the board's own RGB LED, if it's visible through the case. |
| **IMU** | An LSM6DSO or similar on the I2C header | Real g-forces (a friction circle), and real acceleration in the RaceBox packet for phone apps. |
| **CAN / OBD-II** | An SN65HVD230 transceiver on IO3 and IO14, and an OBD-II plug | RPM, throttle and temperatures from the car (the ESP32-S3 has a CAN controller built in): a shift light on the LED bar, and more channels in the log. This is the step from lap timer to data logger, like AiM's Solo 2 DL. The most work, and different for each car. |

**IMU status: done, untested on hardware** (2026-10-20, commit 91794f5). An LSM6DSO-class IMU on the I2C header is found at boot (`IMU_ENABLED`, default on, harmless with none), learns how it's mounted, and then gives the log and the RaceBox packet real g. Only the maths is host-tested.

**CAN / OBD-II status: done, untested on hardware** (2026-10-20, commit 7b26c4e). TWAI on IO3/IO14 with an SN65HVD230; engine speed, throttle and coolant temperature into the log, and a shift light on the LED bar. `CAN_ENABLED` is off by default (IO14 is a GPS pin on the header). Only the OBD-II parsing, polling and shift light are host-tested.

### Phase 6: Engine data and video

The goal: engine data (RPM, throttle, temperatures) from a plug-in Bluetooth OBD-II adapter, and videos from an Osmo Action 4 or an iPhone with the track map, speed, g-force and engine data drawn on top.

**Track Rat becomes the hub.** It reads the adapter, tells the camera where it is, and passes everything to the phone:

```
Bluetooth OBD-II adapter --> Track Rat --> SD card: the log, now with engine channels
                                |
                                +--> Osmo Action 4: Track Rat's GPS, 10 a second, written into the video;
                                |    recording starts and stops with each session
                                +--> iPhone (RaceChrono Pro): GPS and engine data; the phone films and draws the overlay
```

Why a hub: a Bluetooth adapter talks to one device at a time. If the phone takes it, Track Rat has nothing to log or show; with Track Rat holding it, every video path gets the engine data.

**What to buy**

| Part | Notes |
|---|---|
| A **Bluetooth LE** OBD-II adapter, e.g. Veepeak OBDCheck BLE+ (about $30) | It must be a BLE model. The ESP32-S3 has no Classic Bluetooth, so the Veepeak Mini (Bluetooth), Wi-Fi adapters and the OBDLink MX+ can't connect. One review says the 2026 BLE+ uses an STN2120 chip, faster than the usual ELM327 clones. |
| Or the OBDLink CX | BLE with an STN chip, the fastest BLE adapter RaceChrono lists. CAN cars only (most cars from 2008 on). |
| An IMU, optional | Already supported (5.1): an LSM6DSO breakout on the I2C header gives real g. Without one, g comes from the GPS, which is fine for overlays. |
| Nothing for the camera | Track Rat does the job of DJI's GPS remote (6.2). |
| RaceChrono Pro on the iPhone | A paid app. It films with the phone and draws the overlay (6.3). |

**6.1 Engine data from a Bluetooth adapter (M)**

- **Pairing:** Settings → Engine data → Find adapter. A 10 s scan lists adapters by name and signal strength; tap one. Track Rat remembers it and reconnects by itself whenever it's in range. An OBD tag in the header shows it's connected, like the BT tag.
- **Talking to it:** adapters speak ELM327, a text protocol, over a BLE service (FFF0 on Veepeak). Sources disagree on which of its two characteristics notifies and which is written, and it varies between models, so find them by their properties rather than fixed UUIDs.
- **Set-up:** echo, spaces and headers off (`ATE0`, `ATS0`, `ATH0`), automatic protocol (`ATSP0`), aggressive adaptive timing (`ATAT2`) and a short timeout (`ATST`). Then ask which PIDs the car supports (`0100`, `0120`, `0140`).
- **How fast:** every request is a round trip over Bluetooth, and the rate is shared between channels. Cheap adapters manage about 6–15 requests a second. With adaptive timing and the "one reply" suffix (`010C1`: the adapter answers as soon as the engine does instead of waiting out its timeout), an ELM327 reaches 20–40 a second on a CAN car; RaceChrono quotes up to 50 Hz for OBDLink. Several PIDs in one request (`010C11`) helps where the adapter supports it. Detect both features and fall back if they fail.
- **Schedule:** reuse 5.2's: RPM most often, then throttle, with coolant and intake temperature about once a second. At 20 requests a second that's RPM at about 10 Hz and throttle at about 7 Hz: plenty for the log and overlays. The shift light trails by about 0.1 s; the CAN transceiver (5.2) is the fix if that matters.
- **Timing:** each value is stamped with GPS time at the middle of its request, so it lines up with the video.
- **Same engine data as 5.2:** the log's engine channels, the shift light on the LED bar, and a coolant warning. The adapter is just another way in. A diagnostics screen shows requests a second and each channel's rate.
- **Code:** the ELM327 protocol (command queue, reply parsing for `NO DATA`, `SEARCHING...`, `?` and the `>` prompt) is pure C++, tested on the PC with recorded adapter conversations. The NimBLE central code is separate.
- **Stretch:** the gear, from RPM divided by speed, once each gear has been seen.

**6.2 Track Rat as the camera's GPS remote (M/L)**

- The Action 4 records GPS only from DJI's GPS remote. DJI publishes an official, MIT-licensed demo of that remote running on an ESP32 (the DJI R SDK protocol), and an open-source Android app, Telemet, does the same from a phone. Port the demo's protocol layer to Track Rat.
- **Pairing, once:** Track Rat sends a connection request, the camera shows a code, and you accept it if it matches Track Rat's screen. After that it reconnects by itself.
- **GPS into the video:** 10 times a second, Track Rat sends what its GPS already gives: position, height, north/east/down speed, accuracies and satellites. DJI's GPS packet has exactly these fields. The camera writes them into the video file, and DJI Mimo and DJI Studio then draw speed, route and altitude dashboards with no other software.
- **Recording follows the session:** the camera starts when you leave the pits and stops when you park (1.3's pit detection). A setting chooses this or manual. The header shows the camera's recording state and battery, and warns if it's overheating; the camera reports all three.
- **Sync for richer overlays:** Track Rat notes, in GPS time, when the camera started recording, so 6.4 can line the log up with the video without sliding it by hand.
- **Caveats:** the camera has one remote slot, so DJI Mimo can't be its remote at the same time; the protocol is subject to DJI's licence terms; DJI's time field is offset by +8 hours (check it on the camera). Untested until it's tried on your Action 4.

**6.3 Engine data to the phone: a RaceChrono profile (M)**

- RaceChrono's DIY Bluetooth LE API (iOS and Android) takes GPS and CAN-style data from homemade devices. Track Rat sends its 20 Hz GPS and each engine value as a CAN packet, and in RaceChrono you add a channel per value. The README gets ready-made channel definitions.
- RaceChrono Pro then films with the iPhone and draws speed, the map, lap times, g, RPM and throttle live. It can also link Osmo Action videos afterwards and sync them.
- A Bluetooth setting chooses the profile: **RaceBox Mini** (works with most apps) or **RaceChrono** (GPS and engine data), one at a time. A RaceChrono forum report found iOS stopped receiving DIY data when a second UART-style service was present, and the RaceBox profile is one.

**6.4 Overlay videos on the PC (L)**

- **Tools that already read our files:** RaceRender reads VBO (its free edition is limited to 3 minutes with a logo; $40–60 to unlock). Telemetry Overlay ($199, one-time) reads DJI Action GPS and CSV or GPX. Add a CSV download for them with plain column names: time, lat, lon, speed, lap, delta, g, rpm, throttle.
- **Our own, free:** `tools/overlay.py` reads a session's VBO and draws the overlay in Track Rat's style:
  - track map with your dot
  - speed
  - lap time and delta
  - sector colours
  - a g circle
  - RPM and throttle bars
  - coolant

  ffmpeg then either burns it onto the video or makes a transparent overlay to drop into an editor (DaVinci Resolve, Final Cut, CapCut).
- **Lining it up:**
  1. Osmo Action with 6.2: automatic, from the recording-start time Track Rat noted.
  2. iPhone video: the file's creation time gets within a second; nudge the rest against a frame preview.
  3. Any camera: tap for a sync flash, and Track Rat's screen shows a big SYNC with the GPS time. Film it and mark that frame.
- Python rendering is slow for long sessions. Render at 30 fps and redraw only what changes.

**Order:** 6.1 first (one purchase, the biggest data win), then 6.2 (no purchase, the biggest video win), then 6.3, then 6.4. 6.1 to 6.3 can be built and host-tested now; they need your adapter, camera and phone to confirm.

**Risks to test early**

- **Three Bluetooth links on one radio** (phone, adapter, camera). NimBLE supports it, but scanning and connecting while the others run needs care. Prove it on the bench before building on it.
- **Adapters differ.** Detect features rather than assume them.
- **Cars differ.** Some report throttle only as pedal position (PIDs 0x45 or 0x49), and older non-CAN cars manage only a few readings a second.
- **Engine data lags** reality by about one Bluetooth round trip (50–100 ms). The timestamps take that into account.

## Not planned, and why

- **Video recording on Track Rat.** No camera input or video encoder. Cameras and phones do it better, so Phase 6 helps them instead: Track Rat feeds its GPS into an Osmo Action's video, starts and stops it with each session, and feeds GPS and engine data to RaceChrono on a phone.
- **Spoken coaching.** Garmin's reaches the driver through earbuds or the car's stereo. The ESP32-S3 has no Classic Bluetooth for audio, and the board's speaker won't be heard under a helmet. Use colour and the LED bar instead.
- **Racing-line coaching.** About 1.5 m of GPS error is as big as the line differences that matter. Time and speed comparisons are reliable; line position isn't.
- **A phone app.** The Wi-Fi page covers it without app stores, and RaceBox emulation already covers live phone use.
- **Fully configurable dash pages.** AiM offers several configurable pages. Three good screens with a couple of options keeps Track Rat simple.
- **Cloud and leaderboards.** Many tracks have no signal. Maybe later: sync when home Wi-Fi is in range.

## Order of work

Build the phases in order. Within Phase 1, do 1.1 to 1.5 first; 1.6 to 1.8 can go in anywhere. Some exceptions:

- The Wi-Fi page (3.1) only needs the log (1.1) and settings (1.4). Bring it forward if taking the card out to get files becomes a chore.
- Acceleration runs (4.3) only need the log, but are better with binary GPS (1.7) for slope.
- The big button and LED bar can be added at any point after settings (1.4).

Two habits that will pay off:

- **Keep new logic free of Arduino code**, as `LapTimer` and `FreeRoam` are, so session rules, sectors, the VBO writer and acceleration runs are tested on the PC like the lap timer.
- **Let the Bench Test replay a real log** instead of driving its circle. Every feature can then be tried at a desk with real data, and it's the same code re-timing needs.

## Smaller ideas for later

- A stint timer for endurance racing, with a warning before the maximum stint time.
- Several drivers sharing one unit, each with their own PBs.
- A shareable image of a session (map, best lap, date) from the Wi-Fi page.
- Satellite imagery in the track editor when the phone has internet.

## Open questions

- How readable is the screen at noon in an open car or kart? That decides how much the LED bar matters.
- Does the BE-252Q keep its satellite data between power-ups? With backup power it gets a fix in about 1 s; without, it can take about 30 s.
- Default delta reference: best of the day (fair to the day's conditions) or best ever (motivating)? This plan uses best ever until you set a lap that day.
- Is 10 minutes the right gap before a new session starts?
- Is the board's RGB LED visible through your case?
- Battery size: measure the current draw first.
- Does Circuit Tools open the VBO files? Write a sample early.
- Which OBD-II adapter will you use (Veepeak OBDCheck BLE+ or OBDLink CX), and what car? A CAN car (2008 on) is far faster than an older one.
- Do you already own DJI's GPS remote for the Action 4? Track Rat can replace it, but the camera has only one remote slot.

## Sources

- [LCDWiki: ES3C28P board](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display): battery charger and IO9 voltage, speaker amp, microphone, RGB LED, free pins, measured brightness
- [Beitian BE-252Q listing](https://rcdrone.top/products/beitian-be-122-be-182-be-252q-gps-module): M10050 chip, accuracy, start-up times
- [u-blox change notice on M10 update rates](https://shop.richardsonrfpd.com/docs/rfpd/u-blox_PCN_UBX-23006557.pdf): 25/20/16/10 Hz by number of satellite systems
- [AiM Solo 2 DL](https://discoveryparts.com/products/aim-sports-solo-2-dl-data-logging-lap-timer) and [AiM shop](https://aimshop.com/products/aim-solo-2-dl-gps-ecu-dash-logger): predictive lap, LEDs, theoretical lap, auto-learning, ECU data
- [VBOX LapTimer predictive timing menu](https://en.racelogic.support/motorsport/lap-timing/vblap/ug/menu-options/predictive-lap-timing-menu/): delta bar, speed mode for pit lanes, LEDs
- [Apex Pro Gen II](https://www.hmsmotorsport.com/item/apex-pro-gen-ii): LED coaching and predictive LEDs
- [Garmin Catalyst 2 at BimmerWorld](https://www.bimmerworld.com/About-Us/Data-Acquisition-101/Garmin-Catalyst-2-Driving-Performance-Optimizer-Camera.html): audio coaching, True Optimal Lap, top three opportunities
- [Your Data Driven: Catalyst optimal lap review](https://www.yourdatadriven.com/garmin-catalyst-review-optimal-lap-any-good/) and [NA M3 Forum: Catalyst after two days](https://nam3forum.com/forums/forum/special-interests/track-autox/60183-garmin-catalyst-review-after-2-days-on-track): what owners found useful and misleading
- [Which lap timer should you buy? (gt.si)](https://gt.si/which-lap-itmer-should-you-buy/): phone app, RaceBox, Catalyst and VBOX compared in use
- [RaceBox Mini listing](https://modeautoconcepts.com/fr-ca/products/racebox-mini-automotive-performance-lap-timer-drag-meter): acceleration and braking modes, slope correction
- [MyChron 6 at Perry Auto](https://perryautolaval.com/products/aim-mychron6-kart-lap-timer-data-logger): Wi-Fi download without a PC
- [RaceChrono changelog](https://racechrono.com/article/2025): predicted lap time, VBO export
- [Racelogic: VBO file structure](https://developers.racelogic.co.uk/Portal/Tutorials/tutorialVBOfilestructure.html) and [Circuit Tools](https://vboxmotorsport.co.uk/us/circuit-tools)
- [Autosport Labs forum: autocross start and finish](https://direct.forum.autosportlabs.com/viewtopic.php?p=19198): standing starts and gate placement

Phase 6:

- [RaceChrono: which OBD-II adapter should I buy?](https://racechrono.com/article/faq/which-obd-ii-adapter-should-i-buy): OBDLink up to 50 Hz, BLE adapters 10–15 Hz, the rate shared between channels
- [Veepeak OBDCheck BLE instructions](https://veepeak.com/pages/obdcheck-ble-user-instructions), [Harry's LapTimer forum: Veepeak BLE UUIDs](http://forum.gps-laptimer.de/viewtopic.php?t=5332) and [esphome-obd2-ble](https://github.com/rubenmuehlhans/esphome-obd2-ble): service FFF0, characteristics FFF1/FFF2, ESP32 BLE-only
- [Botmonster: Veepeak vs OBDLink over BLE](https://botmonster.com/smart-home/home-assistant-vehicle-tracking-obd2-ble/): BLE+ chipset, ESP32 reconnect advice
- [ELM327 datasheet](https://cdn.sparkfun.com/assets/learn_tutorials/8/3/ELM327DS.pdf): adaptive timing and the expected-replies digit
- [RaceChrono BLE DIY device API](https://github.com/aollin/racechrono-ble-diy-device) and [tutorial](https://racechrono.com/article/2572): GPS and CAN characteristics, filters
- [RaceChrono forum: iOS and an extra UART service](https://racechrono.com/forum/discussion/2678/racechrono-ios-doesnt-work-with-additional-ble-uart-service-on-my-device)
- [DJI Osmo GPS Controller demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo) (ESP32, MIT): GPS push, recording control, camera status; supports Action 4, 5 Pro, 6 and Osmo 360
- [Telemet](https://github.com/avsessisozx/Telemet): a phone as the Action 4's GPS remote; BLE service FFF0, notify FFF4, write FFF5
- [Telemetry Overlay for DJI Action](https://goprotelemetryextractor.com/tools-for-dji-action): GPS only with the remote; $199 one-time
- [RaceRender 3 features](https://racerender.com/RR3/Features.html): VBO and CSV; editions and limits
- [RaceChrono Pro on the App Store](https://apps.apple.com/us/app/racechrono-pro/id1129429340): phone video with data overlays, external GPS, linking external camera video
