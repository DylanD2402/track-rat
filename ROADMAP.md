# Track Rat feature plan

What to build next. It's based on what this hardware can and can't do, how people use lap timers at real events, and what the commercial timers (AiM, Garmin, VBOX, Apex Pro, RaceBox) and phone apps (RaceChrono, Harry's) get right and wrong.

**Now (2026-10-25):** Phases 1 to 6 are built. Next is [Phase 7](ROADMAP-ARCHIVE.md#phase-7-the-01-beta-a-release-anyone-can-use), the 0.1 beta release, then the [Phase 8](#phase-8-candidates-for-02-researched-2026-10-25) candidates.

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

Plus a cheap, optional add-on: a big glove-friendly button.

## What the hardware allows

| Hardware | What it means for features |
|---|---|
| 2.8" 320×240 screen, about 230 nits measured | Hard to read in direct sun (sunlight-readable displays are around 1,000 nits). Use big colour blocks and few numbers. |
| Capacitive touch | Doesn't work with racing gloves, and vibration and rain cause false taps. Nothing on track may need a touch. In the pits, use screen thirds or a button. |
| BOOT button | The only physical input, and it's on the board, so hard to reach in a case. A button on a spare pin fixes this. |
| ESP32-S3 Wi-Fi and Bluetooth LE | A web page on your phone can replace an app. Bluetooth already streams to phone apps as a RaceBox Mini. There's no Classic Bluetooth, so no audio to headsets or car stereos. |
| 240 MHz dual core, 8 MB PSRAM | Analysing a whole session on the device is easy. |
| 16 MB flash: two 6.25 MB app slots, 3.4 MB LittleFS | Firmware is 0.8 MB, so over-the-air updates fit, and so do the web page's files. |
| microSD | Every fix at 20 Hz is about 7 MB per hour of driving. A card holds years of it. |
| USB power from the car | Cuts out when the ignition goes off, at any moment. Saving must survive that. |
| Battery socket and charger, battery voltage on IO9 | Can run on a kart, keep going when the car is switched off, and show a battery gauge. |
| Free pins IO2, IO3, IO14, IO21; I2C header shared with touch | Room for a button, an IMU or a CAN (OBD-II) adapter. |
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

### Phases 1 to 7, Phase 9 and 0.1.1: built

These phases are built. Their write-ups moved, word for word, to [ROADMAP-ARCHIVE.md](ROADMAP-ARCHIVE.md) on 2026-11-14, so this file holds what's still open. Section numbers such as 1.6 or 4.3 in the text below refer to that file.

### Phase 8: Candidates for 0.2 (researched 2026-10-25)

Ranked by value for the effort. Each "needs" is beyond the parts Track Rat already uses.

| # | Feature | Why | Needs | Size | Built by |
|---|---|---|---|---|---|
| 1 | **Analysis in the browser:** speed and delta against distance, two laps on the map, sectors, a friction circle. On the Wi-Fi page and on the project website, which opens a `.vbo` from your PC without uploading it | Review at the track with no PC software. The website also lets anyone try Track Rat on sample sessions | Nothing | L | Sonnet, medium (2–3 briefs) |
| 2 | **Tracks worldwide** from OpenStreetMap: the Canada fetch generalised to any country (US, UK, Europe, Australia), maps simplified to fit | Auto-detection everywhere, not just Canada. Commercial timers advertise 1,000–4,000 tracks | Nothing | M | Sonnet, medium |
| 3 | **Drag-race tree** in Performance: staging, ambers or a pro tree, reaction time, 60 ft, red light | Turns Performance into a drag-strip tool | Nothing | M | Sonnet, medium |
| 4 | *Sound (beeps, a buzzer) was dropped in 0.1.1; the number stays so the steps below keep theirs* | | | | |
| 5 | **Driver and car profiles:** sessions and PBs per driver and per car | A shared car or a club unit | Nothing | S | Sonnet, low |
| 6 | **Someone else's reference lap:** load a friend's or a coach's `.vbo` on the Wi-Fi page and drive against it | The quickest way to find time | Nothing | M | Sonnet, medium |
| 7 | **Conditions quick-picks:** dry, damp or wet; tyres; temperature; saved with the session | Days become comparable | Nothing | S | Haiku, low |
| 8 | **Share learned tracks:** export and import a track file on the Wi-Fi page | Tracks stop being stuck on one unit | Nothing | S | Sonnet, low |
| 9 | **Heart rate** from a Bluetooth chest strap (standard service 0x180D), logged and in the overlay | Karting and endurance drivers use it | A strap | S | Sonnet, low |
| 10 | **GoPro HERO9 and later:** recording starts and stops with the sessions over Bluetooth (Open GoPro), like the DJI camera | Most track-day cameras are GoPros | A GoPro to test | M | Sonnet, medium |
| 11 | **Tyre pressures and temperatures** from Bluetooth valve-cap sensors. Formats differ by brand: buy a set, record its broadcasts, then decode | Pressures matter for every session | Sensors, ~$20–30 | M | Sonnet, medium |
| 12 | **A Bluetooth button on the wheel** for the SYNC flash, a note, or ending the session | No reaching for the screen | A clicker, ~$5 (bonding varies) | S–M | Sonnet, low |
| 13 | **Update the Wi-Fi page and track list without a PC:** the Wi-Fi page and the SD card also take the file-system image | Today only the app updates that way | Nothing | S | Sonnet, low |

**Looked at, not worth it:**
- **A dual-band GPS swap.** No small dual-band module manages 20 Hz (the u-blox F10 and Quectel LC29H top out at 10 Hz), and the BE-252Q's 20 Hz already matters more for timing.
- **Voice over Bluetooth LE Audio.** The ESP32-S3 doesn't support it.
- **A live view for the crew.** Wi-Fi won't reach the pits.

Phase 8 starts after the 0.1 beta ships, so its README changes don't collide with 7.5.

**Picked first (2026-10-25): 8.1 and 8.5.** They're built in two waves. In each wave the steps touch different files, so they run side by side.

| Wave | Step | Built by | Touches |
|---|---|---|---|
| 1 | 8.1a The viewer: the Wi-Fi page reads local files, a sample session, and trackrat.ca/analyze/ | Sonnet, medium | web/index.html, site/, installer.yml, tools/, test_web.js |
| 1 | 8.5a Drivers and cars on the device | Sonnet, medium | firmware, its tests, the screen renders |
| 2 | 8.1b New views: channel traces, friction circle, sector table, linked cursor | Sonnet, medium | the Compare tab of web/index.html |
| 2 | 8.5b Drivers and cars on the Wi-Fi page | Sonnet, low | the Settings and Sessions tabs of web/index.html |

#### 8.1 Analysis in the browser: the plan
**Status: done** (2026-10-25): 8.1a 4c05845 (the viewer, live at https://trackrat.ca/analyze/), 8.1b 060e549 (channel traces, friction circle, sector table, shared cursor). Tried in a browser with the sample session; not yet on the board's own page.
The Wi-Fi page already compares two laps: speed and gap against distance, and the map coloured by the faster lap. It also has a season view, all tested in test_web.js. So 8.1 grows that page rather than starting another, and the website uses the same page.

- **8.1a The viewer.** The same web/index.html runs off the board too, in a viewer mode:
  - **Opening files:** the page opens `.vbo` and `.json` files picked or dropped from your PC; nothing is uploaded. The Sessions and Compare tabs work on them, and the board-only tabs (Firmware, Device, Settings, Files, uploads) are hidden.
  - **The sample:** `tools/make_sample_session.py` makes a sample session: a simulated car at Calabogie from tracks.json's map, labelled as simulated.
  - **The website:** installer.yml publishes the page at **trackrat.ca/analyze/**, with its fonts and the sample, and the install page links to it.
  - **On the board:** nothing changes there. Font links become relative so both places work.
- **8.1b New views,** on the board and the website alike:
  - traces of any channel the log has, against distance: RPM, throttle, temperatures, the OBDb car channels (brake pressure, steering), the IMU's g;
  - a friction circle (lateral against longitudinal g) for each lap;
  - a sector table from the summaries' `sectorsMs`;
  - a cursor shared by the charts and the map.

  The new maths lives in the page's tested sections.

#### 8.5 Driver and car profiles: the plan
**Status: done** (2026-10-25): 8.5a (on the device) and 8.5b 2f8fd04 (the Wi-Fi page). Not yet tried on the board: NVS, the per-pair PB folders on a card, /api/profiles over Wi-Fi, and the VIN.
- **8.5a On the device.**
  - **The list:** up to 8 drivers and 8 cars, kept in their own module, not in the settings the Wi-Fi page mirrors. Each has a fixed id and a name; a car can also have a VIN.
  - **On screen:** Settings gets Driver and Car rows. The Ready screen and the track list show "driver · car" once there's more than one of either.
  - **PBs** are kept per driver and car. The first driver and first car keep today's `/tracks/<id>/` files, so existing PBs stay.
  - **Session files:** summaries and `.vbo` comments name the driver and the car.
  - **VIN:** a car whose VIN the Bluetooth adapter reports is picked by itself.
  - **The Wi-Fi API:** `GET`/`POST /api/profiles`.
- **8.5b On the Wi-Fi page:**
  - add, rename and remove drivers and cars, and choose the current ones;
  - the session list shows each session's driver and car.
  
  Filters in Compare can follow later.

### Phase 10: Every track in the world (proposed 2026-10-26)

Today `data/tracks.json` holds 129 Canadian tracks from OpenStreetMap; 8 have a start/finish line. The goal is every paved circuit and kart track in the world, with its layout, so a new track works the first time you drive it.

- **What OpenStreetMap has:** circuits and pit lanes (`highway=raceway`), venues (`leisure=track`, `sport=motor|karting`), names, and often an English name (`name:en`). It doesn't record the start/finish line.
- **Start/finish, estimated:** on a circuit with a mapped pit lane, the line is usually on the main straight beside it. Take the point on the circuit closest to the pit lane's middle, heading the way the lap runs. Save it as `"estimated": true`. The device says "Start/finish estimated" and offers Adjust; the self-learning lap (4.1) and tap-at-the-line still work. Checked against the tracks whose line is known.
- **Size:** about 2 KB a track, 10,000 or more tracks: over 20 MB. That's far beyond the 3.4 MB LittleFS partition, and too much to parse at boot. So the world set lives on the SD card in 2° tiles, `/world/<lat>/<lon>.json` (each the corner's whole degrees, even numbers; the same schema as `tracks.json`). The device loads the tile it's in, plus a neighbour when near an edge.
- **Your tracks win:** `tracks.json` stays the user's file. A world track with the same id, or within 1 km of one of yours, is hidden. Editing a world track (start/finish, a layout, a name) copies it into `tracks.json` first.
- **Names:** the English name when there is one (the local one kept as `localName`). Text the screen's fonts can't draw is folded to plain letters (é → e); other scripts fall back to the English name.
- **Getting it onto the card:** a "World tracks" workflow fetches the data from Overpass country by country (by hand; the monthly schedule was turned off) and publishes `trackrat-world-tracks.zip`. You unzip it onto the SD card.
- **Licence:** OpenStreetMap data is ODbL: credited in `DATA_LICENSE.md`, and the published tiles stay under ODbL.

| Wave | Step | Built by |
|---|---|---|
| 1 | **Accents on the screen:** fold characters the fonts don't have to plain letters, in one place in the drawing code | Sonnet, low |
| 1 | **The world data pipeline:** `tools/fetch_world_tracks.py` (shared code with the Canada tool), names, kart/dirt/drag rules, the start/finish estimate, 2° tiles, tests; tried on a few countries | Sonnet, medium |
| 1 | **The device reads world tiles:** load the nearby tiles from the SD card, merge with `tracks.json`, copy a world track before editing it, show an estimated start/finish | Sonnet, medium |
| 2 | **The workflow and the zip** (done: `world-tracks.yml`, run by hand, rolling `world-tracks` release; docs), the full world run, and the website's analyser naming sessions from the tiles (to do) | Sonnet, low |

### Phase 11: Any screen, and other boards (proposed 2026-11-04)

Run Track Rat on panels other than the ES3C28P's 320×240: bigger, smaller and portrait ones, other touch chips, and boards whose makers ship LVGL. Board files first, then main.cpp split into screen controllers, one drawing interface, layouts that scale, fonts per size, a second board, and an optional LVGL renderer last. The design, the steps and who builds them: [docs/display-plan.md](docs/display-plan.md). The clean-up review that goes with it: [docs/code-review-0.2.md](docs/code-review-0.2.md).

## Not planned, and why

- **Video recording on Track Rat.** No camera input or video encoder. Cameras and phones do it better, so Phase 6 helps them instead: Track Rat feeds its GPS into an Osmo Action's video, starts and stops it with each session, and feeds GPS and engine data to RaceChrono on a phone.
- **Spoken coaching.** Garmin's reaches the driver through earbuds or the car's stereo. The ESP32-S3 has no Classic Bluetooth for audio, and Use colour on the screen instead.
- **Racing-line coaching.** About 1.5 m of GPS error is as big as the line differences that matter. Time and speed comparisons are reliable; line position isn't.
- **A phone app.** The Wi-Fi page covers it without app stores, and RaceBox emulation already covers live phone use.
- **Fully configurable dash pages.** AiM offers several configurable pages. Three good screens with a couple of options keeps Track Rat simple.
- **Cloud and leaderboards.** Many tracks have no signal. Maybe later: sync when home Wi-Fi is in range.

## Order of work

Build the phases in order. Within Phase 1, do 1.1 to 1.5 first; 1.6 to 1.8 can go in anywhere. Some exceptions:

- The Wi-Fi page (3.1) only needs the log (1.1) and settings (1.4). Bring it forward if taking the card out to get files becomes a chore.
- Acceleration runs (4.3) only need the log, but are better with binary GPS (1.7) for slope.
- The big button can be added at any point after settings (1.4).

Two habits that will pay off:

- **Keep new logic free of Arduino code**, as `LapTimer` and `FreeRoam` are, so session rules, sectors, the VBO writer and acceleration runs are tested on the PC like the lap timer.
- **Let the Bench Test replay a real log** instead of driving its circle. Every feature can then be tried at a desk with real data, and it's the same code re-timing needs.

## Smaller ideas for later

- A stint timer for endurance racing, with a warning before the maximum stint time.
- A shareable image of a session (map, best lap, date) from the Wi-Fi page.
- Satellite imagery in the track editor when the phone has internet.

## Open questions

- How readable is the screen at noon in an open car or kart?
- ~~Does the BE-252Q keep its satellite data between power-ups?~~ Answered (2026-10-24): only for as long as its small backup capacitor lasts, and orbits go stale after 4 hours anyway. Track Rat now keeps the GPS powered in standby whenever the board sleeps or is switched off (1.6), with AssistNow Autonomous on. Unplugging the battery still means a cold start.
- Default delta reference: best of the day (fair to the day's conditions) or best ever (motivating)? This plan uses best ever until you set a lap that day.
- Is 10 minutes the right gap before a new session starts?
- Battery size: measure the current draw first.
- Does Circuit Tools open the VBO files? Write a sample early.
- Which OBD-II adapter will you use (Veepeak OBDCheck BLE+ or OBDLink CX), and what car? A CAN car (2008 on) is far faster than an older one.
- Do you already own DJI's GPS remote for the Action 4? Track Rat can replace it, but the camera has only one remote slot.

## Sources

- [LCDWiki: ES3C28P board](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display): battery charger and IO9 voltage, free pins, measured brightness
- [Beitian BE-252Q listing](https://rcdrone.top/products/beitian-be-122-be-182-be-252q-gps-module): M10050 chip, accuracy, start-up times
- [u-blox change notice on M10 update rates](https://shop.richardsonrfpd.com/docs/rfpd/u-blox_PCN_UBX-23006557.pdf): 25/20/16/10 Hz by number of satellite systems
- [u-blox M10 SPG 5.10 interface description](https://cdn.sparkfun.com/assets/f/1/2/9/e/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf): UBX-RXM-PMREQ, UBX-NAV-AOPSTATUS, CFG-ANA-USE_ANA (off by default)
- [u-blox SAM-M10Q integration manual](https://cdn.sparkfun.com/assets/4/1/1/0/3/SAM-M10Q_IntegrationManual_UBX-22020019.pdf): software standby keeps the clock and orbits but clears RAM settings, hot start within 4 hours, AssistNow Autonomous 3 to 6 days
- [BE-252Q manual](https://manualsfile.com/product/k6aaab5d5ea3.html): the backup capacitor that keeps its clock with the power off
- [AiM Solo 2 DL](https://discoveryparts.com/products/aim-sports-solo-2-dl-data-logging-lap-timer) and [AiM shop](https://aimshop.com/products/aim-solo-2-dl-gps-ecu-dash-logger): predictive lap, theoretical lap, auto-learning, ECU data
- [VBOX LapTimer predictive timing menu](https://en.racelogic.support/motorsport/lap-timing/vblap/ug/menu-options/predictive-lap-timing-menu/): delta bar, speed mode for pit lanes
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

Phases 7 and 8:

- [ESP Web Tools](https://esphome.github.io/esp-web-tools/): install from the browser; the manifest, ESP32-S3 parts, CORS for files on another host
- [GitHub community: release assets send no CORS headers](https://github.com/orgs/community/discussions/45446), and [WLED's installer](https://install.wled.me), which mirrors its binaries for that reason
- [esptool: merge-bin and the v5 migration](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/migration-guide.html)
- [Semantic Versioning](https://semver.org) and [Keep a Changelog](https://keepachangelog.com)
- [OpenStreetMap attribution guidelines](https://osmfoundation.org/wiki/Licence/Attribution_Guidelines) and [copyright](https://www.openstreetmap.org/copyright): "© OpenStreetMap contributors", ODbL share-alike for the track list
- [LCDWiki ES3C28P](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display): native USB, download mode
- [u-blox MAX-F10S datasheet](https://mouser.com/datasheet/2/1025/MAX_F10S_DataSheet_UBXDOC_963802114_12732-3502504.pdf) and [Quectel LC29H specification](https://quectel.com/content/uploads/sites/3/2022/06/Quectel_LC29H_GNSS_Specification_V1.2.pdf): dual-band modules top out at 10 Hz
- [Open GoPro](https://gopro.github.io/OpenGoPro/): Bluetooth control for HERO9 and later
- [andi38/TPMS](https://github.com/andi38/TPMS): one valve-cap TPMS family's broadcast format; others differ
- [DovesLapTimer](https://github.com/TheAngryRaven/DovesLapTimer): an open-source Arduino GPS lap timer, for comparison
- [Garmin Catalyst](https://www.garmin.com/en-AU/blog/?p=4181) and [MyChron 6](https://www.aimshop.com/blogs/lap-timers/mychron-6-lap-timer-the-ultimate-kart-racing-upgrade): what commercial timers lead with
