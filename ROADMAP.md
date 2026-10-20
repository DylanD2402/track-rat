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
- **While moving,** ignore quick taps (vibration, rain). A firm press switches the view.
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

**Status: done** (2026-10-20, commit COMMIT) for text notes. Voice notes wait behind `VOICE_NOTES_ENABLED` (0) in `Config.h` until the ES8311 codec is brought up on a board.

### Phase 4: More ways to drive

**4.1 Unknown tracks learn themselves (M)**

- Today, away from known tracks you get a menu, and if you just drive off, nothing is saved.
- Instead, start recording when you move off. When you drive the same loop twice (back past a point you passed at speed, heading the same way, at least 20 s and 300 m later), it's a track. Track Rat creates it, puts a provisional line where the loop began, times both laps from the log, and times every lap after that. Move the line later (3.2) and re-time. Needing two loops keeps a drive round the block from becoming a track.
- AiM's Solo 2 does something similar ("auto-learning") for tracks that aren't in its database.
- NEW TRACK's tap-at-the-line stays, for anyone who wants the exact line straight away.
- With road drive recording off, Track Rat still watches for a loop but doesn't save the drive.

**4.2 Point to point: autocross, hillclimbs, sprints (M)**

- A track type with separate start and finish lines, plus splits.
- **Standing start:** armed while stopped near the start. The clock starts at the first movement, or at a start line a few metres on (timing lights usually sit a little past the line).
- **Finish:** crossing the finish line. If none is set yet, tap as you cross; or the run ends when you stop, and you set the finish afterwards and re-time.
- Each run counts as a lap: best run, live delta to it, sector colours.
- **NEW COURSE** makes a stage named with the date, since autocross courses change at every event.

**4.3 Acceleration and braking runs (M)**

- A mode showing big speed, armed while stopped: 0–100 km/h (or 0–60 mph), 100–200 km/h, ⅛ and ¼ mile with trap speed, and 100–0 braking distance. Starts on movement, with an optional 1-foot rollout (the US drag-strip convention). A result card for each run, and a history.
- Warns when the run wasn't level, from the change in height. RaceBox corrects for slope.
- **Why:** acceleration runs are what the RaceBox and Dragy are mostly sold for, and this needs no new hardware.

**4.4 Karting (S)**

- Mostly covered already: battery (1.6), directional lines (1.5). Add a smaller default gate (8 m) and the minimum lap time as a setting.

### Phase 5: Optional add-ons

All of these use the spare pins on the expansion header, with the GPS on the UART connector. (With the GPS on the expansion header instead, only IO2 and IO3 are free.)

| Add-on | Parts | What it adds |
|---|---|---|
| **Big button** | A 25–30 mm push button on IO21 | Does what BOOT does, mounted on the dash or wheel. Works with gloves. |
| **LED delta bar** | 8–12 WS2812B LEDs on IO2 | Delta in peripheral vision: lights grow right in green when ahead, left in red when behind; purple flash for a PB. AiM (10 LEDs), VBOX (6) and Apex Pro all use LEDs for this. The best fix for the dim screen. A first step: show the delta colour on the board's own RGB LED, if it's visible through the case. |
| **IMU** | An LSM6DSO or similar on the I2C header | Real g-forces (a friction circle), and real acceleration in the RaceBox packet for phone apps. |
| **CAN / OBD-II** | An SN65HVD230 transceiver on IO3 and IO14, and an OBD-II plug | RPM, throttle and temperatures from the car (the ESP32-S3 has a CAN controller built in): a shift light on the LED bar, and more channels in the log. This is the step from lap timer to data logger, like AiM's Solo 2 DL. The most work, and different for each car. |

## Not planned, and why

- **Video recording.** No camera input or video encoder. A GoPro does it better, and most GoPros record GPS time, so overlay tools can line the footage up with Track Rat's UTC-stamped logs. The RaceBox Bluetooth link already feeds phone apps that record video with overlays.
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
