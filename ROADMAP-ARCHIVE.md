# Track Rat roadmap archive: the finished phases

Phases 1 to 7, Phase 9 and 0.1.1, moved word for word from [ROADMAP.md](ROADMAP.md) on 2026-11-14 once they were built. The open work (the Phase 8 candidates, Phase 10 and Phase 11) and the background sections stay there.

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
- Power off with no new hardware (asked for 2026-10-24): Settings → Power off, or BOOT held 3 s. The board can't disconnect its battery, so off is a deep sleep, and a tap on the screen turns it back on (or BOOT), like a phone's tap to wake. Unplugging the battery to switch off meant a cold start for the GPS (~30 s), so in every sleep the GPS now goes into standby instead (UBX-RXM-PMREQ, tens of µA), keeping its clock and satellites, with AssistNow Autonomous on for quick fixes after days off.

**Status: power off and GPS standby done** (2026-10-24). Tried on the board: off with BOOT, a tap back on, and the GPS out of standby with a fix 3.3 s after waking, as the logo finished. That test found the 1 s BOOT hold starting Free Roam on the menu on its way to switching off, so BOOT's hold now acts when it's let go (1 to 2 s), and held on it only switches off. The sleep current is still to be measured.

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
- **Trend bar** at one edge: gaining or losing over the last few seconds. VBOX shows a delta bar.
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

**Status: done** (2026-10-20, commit 1b8cf77) for text notes. (Voice notes were planned and removed in 0.1.1.)

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
| **IMU** | An LSM6DSO or similar on the I2C header | Real g-forces (a friction circle), and real acceleration in the RaceBox packet for phone apps. |
| **CAN / OBD-II** | An SN65HVD230 transceiver on IO3 and IO14, and an OBD-II plug | RPM, throttle and temperatures from the car (the ESP32-S3 has a CAN controller built in): and more channels in the log. This is the step from lap timer to data logger, like AiM's Solo 2 DL. The most work, and different for each car. |

**IMU status: done, untested on hardware** (2026-10-20, commit 91794f5). An LSM6DSO-class IMU on the I2C header is found at boot (`IMU_ENABLED`, default on, harmless with none), learns how it's mounted, and then gives the log and the RaceBox packet real g. Only the maths is host-tested.

**CAN / OBD-II status: done, untested on hardware** (2026-10-20, commit 7b26c4e). TWAI on IO3/IO14 with an SN65HVD230; engine speed, throttle and coolant temperature into the log. `CAN_ENABLED` is off by default (IO14 is a GPS pin on the header). Only the OBD-II parsing and polling are host-tested.

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
- **Schedule:** reuse 5.2's: RPM most often, then throttle, with coolant and intake temperature about once a second. At 20 requests a second that's RPM at about 10 Hz and throttle at about 7 Hz: plenty for the log and overlays.
- **Timing:** each value is stamped with GPS time at the middle of its request, so it lines up with the video.
- **Same engine data as 5.2:** the log's engine channels and a coolant warning. The adapter is just another way in. A diagnostics screen shows requests a second and each channel's rate.
- **Code:** the ELM327 protocol (command queue, reply parsing for `NO DATA`, `SEARCHING...`, `?` and the `>` prompt) is pure C++, tested on the PC with recorded adapter conversations. The NimBLE central code is separate.
- **Stretch:** the gear, from RPM divided by speed, once each gear has been seen.

**Status: done, untested on hardware** (2026-10-20, commit c6dc233 and after on queue/6.1). `include/Elm327.h` is the ELM327 conversation (set-up, supported PIDs, the `1` suffix and multi-PID probes with fall-backs, reply parsing across notifications, timeouts and ATZ recovery, values stamped mid-request), host-tested in `test/test_elm327.cpp` against a simulated adapter. `src/ObdBle.cpp` is the NimBLE central in its own task: Settings → Engine data → Find adapter, remembered in NVS, reconnects every 5–10 s, characteristics found by their properties. Engine data from CAN or the adapter feeds the same log channels (now with intake temperature) and a new HOT coolant tag; an OBD tag shows in the header. `OBD_BLE_ENABLED` is on, but nothing happens until an adapter is chosen. No adapter has been tried yet. The gear (stretch) isn't done.

**6.2 Track Rat as the camera's GPS remote (M/L)**

- The Action 4 records GPS only from DJI's GPS remote. DJI publishes an official, MIT-licensed demo of that remote running on an ESP32 (the DJI R SDK protocol), and an open-source Android app, Telemet, does the same from a phone. Port the demo's protocol layer to Track Rat.
- **Pairing, once:** Track Rat sends a connection request, the camera shows a code, and you accept it if it matches Track Rat's screen. After that it reconnects by itself.
- **GPS into the video:** 10 times a second, Track Rat sends what its GPS already gives: position, height, north/east/down speed, accuracies and satellites. DJI's GPS packet has exactly these fields. The camera writes them into the video file, and DJI Mimo and DJI Studio then draw speed, route and altitude dashboards with no other software.
- **Recording follows the session:** the camera starts when you leave the pits and stops when you park (1.3's pit detection). A setting chooses this or manual. The header shows the camera's recording state and battery, and warns if it's overheating; the camera reports all three.
- **Sync for richer overlays:** Track Rat notes, in GPS time, when the camera started recording, so 6.4 can line the log up with the video without sliding it by hand.
- **Caveats:** the camera has one remote slot, so DJI Mimo can't be its remote at the same time; the protocol is subject to DJI's licence terms; DJI's time field is offset by +8 hours (check it on the camera). Untested until it's tried on your Action 4.

**Status: done, untested on hardware** (2026-10-20, queue/6.2). `include/DjiRemote.h` ports the protocol layer of DJI's demo (frame, both CRCs seeded 0x3AA3, connection request and handshake, GPS push, record control, status subscription and push), checked byte for byte against the demo's own frames in `test/test_dji.cpp`. `src/CameraBle.cpp` is the NimBLE central in its own task: Settings → Camera → Pair (DJI adverts: manufacturer data `AA 08 .. .. FA`, the fifth byte as the demo checks it), the code shown on screen, remembered and reconnecting every 5–10 s, GPS at 10 Hz, recording following the sessions (setting *Camera recording*), a CAM/REC tag with the battery and a CAM HOT warning, and `cameraStartUtc` in the summary. The +8 hour is kept as in the demo (`DJI_HOUR_OFFSET`). A shared radio lock keeps the camera and the OBD adapter from connecting or scanning at once. Not yet tried on the Action 4.

**6.3 Engine data to the phone: a RaceChrono profile (M)**

- RaceChrono's DIY Bluetooth LE API (iOS and Android) takes GPS and CAN-style data from homemade devices. Track Rat sends its 20 Hz GPS and each engine value as a CAN packet, and in RaceChrono you add a channel per value. The README gets ready-made channel definitions.
- RaceChrono Pro then films with the iPhone and draws speed, the map, lap times, g, RPM and throttle live. It can also link Osmo Action videos afterwards and sync them.
- A Bluetooth setting chooses the profile: **RaceBox Mini** (works with most apps) or **RaceChrono** (GPS and engine data), one at a time. A RaceChrono forum report found iOS stopped receiving DIY data when a second UART-style service was present, and the RaceBox profile is one.

**Status: done, untested with RaceChrono** (2026-10-20, queue/6.3). `include/RaceChrono.h` has every encoding (GPS main and time with sync bits, CAN packets, the filter), host-tested in `test/test_racechrono.cpp`; `src/RaceChronoBle.cpp` is the 0x1FF8 service with 0x0001–0x0004. **Settings → Bluetooth profile** (RaceBox Mini or RaceChrono) is applied at the next restart rather than by restarting Bluetooth live, as NimBLE doesn't restart reliably on this core; the Wi-Fi page offers Restart now. Engine values go as packets 0x100–0x103 and IMU g as 0x110/0x111, each a big-endian int16 at the filter's interval. The README has the RaceChrono Pro set-up with each channel's equation.

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

**Status: done** (2026-10-20, queue/6.4). `tools/overlay.py` (Pillow, imageio-ffmpeg; pinned in `tools/requirements-overlay.txt`) draws the map and dot, speed, lap, lap time, delta to the best lap, sector colours, a g circle and the engine bars in Track Rat's style with the repo's Inter fonts; output ProRes 4444 `.mov`, VP9-alpha `.webm`, or `--burn` onto the video; frames that don't change aren't redrawn, and it reports its speed and time left. Sync by `cameraStartUtc` (6.2), the video's creation time, a SYNC flash frame (tap the top bar of the summary or debrief: the device shows SYNC and the GPS time for 3 s and notes it as `syncFlashesUtc`), or a given start, each with `--nudge`. The Wi-Fi page's CSV download is made on the phone from the `.vbo`. `test/test_overlay.py` (in CI) renders frames from a synthetic session and checks what they show and the sync maths. Tried end to end on a PC (ProRes, WebM and burn-in); not yet on real footage.

**6.5 Coolant temperature: top bar, overheat warning, pit summary (S/M)**

Asked for on 2026-10-20.

- **The top bar:** the blue OBD tag becomes the live coolant temperature (92°) once the engine has sent one, from CAN or the Bluetooth adapter: blue below *Cold below*, plain between, yellow from *Warm at*, blinking red from *Hot at*. Back to OBD when the value goes stale. The separate HOT tag goes. °F with Speed units in mph.
- **Oil temperature** (PID 0x5C) where the car supports it: asked once a second (by Bluetooth only when the supported-PID bitmap has it; on CAN until three requests go unanswered while the engine answers others), logged as a fifth engine channel "Oil temperature", in the CSV and overlay.py. Not in the top bar; in the warning and the summary.
- **The overheat warning:** at *Hot at* (or *Oil hot at* for oil) a full-screen card flashing red and yellow, COOLANT TOO HOT and the temperature (was a red card, COOLANT 112° or OIL 131°), for 10 s on any screen. Again only after a 3 °C drop and a return, or a minute later still over. A tap closes it. The logic is `include/TempAlert.h`, tested in `test/test_tempalert.cpp`.
- **After the session:** `coolantMaxC` (and `oilMaxC`) for the session and each lap in the summary, recomputed from the log's channels on recovery and re-timing; **Coolant max 104°** on the summary and debrief, and each lap's max on the debrief's laps page.
- **Settings > Coolant:** Cold below 70, Warm at 100, Hot at 110 (COOLANT_WARN_C), Oil hot at 130 °C, 1 °C steps, kept in order; in NVS and on the Wi-Fi page.

**Status: done** (2026-10-24, queue/6.5). Untested on hardware: no OBD-II adapter or CAN transceiver has been tried, and oil temperature depends on the car supporting PID 0x5C.

**Order:** 6.1 first (one purchase, the biggest data win), then 6.2 (no purchase, the biggest video win), then 6.3, then 6.4. 6.1 to 6.3 can be built and host-tested now; they need your adapter, camera and phone to confirm.

**Risks to test early**

- **Three Bluetooth links on one radio** (phone, adapter, camera). NimBLE supports it, but scanning and connecting while the others run needs care. Prove it on the bench before building on it.
- **Adapters differ.** Detect features rather than assume them.
- **Cars differ.** Some report throttle only as pedal position (PIDs 0x45 or 0x49), and older non-CAN cars manage only a few readings a second.
- **Engine data lags** reality by about one Bluetooth round trip (50–100 ms). The timestamps take that into account.

### Phase 7: The 0.1 beta, a release anyone can use

The goal: someone who finds the repo sees what Track Rat is in ten seconds, buys the parts, wires them, installs the firmware from a web page and drives, with no PlatformIO. The first release is **v0.1.0-beta.1**.

| | Today | 0.1 beta |
|---|---|---|
| Installing | PlatformIO, `pio run -t upload` and `-t uploadfs` | A web page with an **Install** button (Chrome or Edge, USB cable). PlatformIO stays for developers |
| Version | A git commit (`8f1b27f`) | `v0.1.0-beta.1` in Settings and on the Wi-Fi page, tagged, with release notes |
| Downloads | None | Every release: a full image, `trackrat.bin` for SD card and Wi-Fi updates, the file-system image, checksums |
| README | 1,120 lines, starting with GPS internals | One screen: what it is, screenshots, buy, wire, install, drive, what's been tried. The detail moves to `docs/` |
| Licences | MIT for the code | Third-party licences listed, OpenStreetMap credit for the track data (ODbL), the Inter font's licence |
| Contributing | Nothing | CONTRIBUTING, issue forms that ask for the firmware version, a PR template |

**How it's built:** from Phase 7 on, each step is a self-contained brief built by a cheaper agent in its own git worktree: Sonnet at medium effort for code, Haiku at low effort for docs and images. Opus writes the briefs, reviews each diff, runs the firmware build and host tests, and merges. No agent flashes the board.

#### 7.1 Release pipeline and web installer (Sonnet, medium)
**Status: done** (2026-10-25, 559b388). The workflow itself runs at the first tag.
- `tools/make_release.py` builds the firmware and file system, then merges one full image with esptool, into `dist/`:
  - Offsets: bootloader `0x0`, partitions `0x8000`, boot_app0 `0xe000`, app `0x10000`, LittleFS `0xc90000`. QIO, 80 MHz, 16 MB.
  - Files: `trackrat-<version>-full.bin`, `trackrat.bin` (the app alone, for the SD card and the Wi-Fi page), `trackrat-<version>-littlefs.bin`, `SHA256SUMS`, and the installer's `manifest.json`.
  - The same script runs on a PC and in CI.
- `.github/workflows/release.yml`, on an annotated tag `v*`:
  - checks out with full history so `git describe` sees the tag;
  - runs the script;
  - creates the GitHub Release, marked pre-release when the tag has `-beta` or `-rc`, with that version's CHANGELOG section as the notes;
  - attaches the files and deploys the installer to GitHub Pages.
- `site/` (GitHub Pages): an install page using a pinned ESP Web Tools.
  - It shows the Install button, what you need, and how to get into download mode (hold BOOT, tap RST).
  - It warns that installing erases sessions saved in the board's internal flash; the SD card is untouched.
  - It serves the binaries itself: a browser can't load them from GitHub Release links (no CORS headers; WLED mirrors its binaries for the same reason).
- `ci.yml`: `fetch-depth: 0`, so CI builds show the version too.
- `docs/flashing.md`: install from the browser, esptool with a downloaded release, SD card and Wi-Fi updates, building from source.

#### 7.2 Screenshots and images (Sonnet, low)
**Status: done** (2026-10-25, 6c2e720). The demo GIF and a photo are still to come.
- `tools/readme_images.py` renders the screens and writes three things:
  - a chosen dozen at 2x to `docs/images/screens/`;
  - `docs/images/gallery.png`, a grid;
  - `docs/images/social-preview.png`, 1280×640 for GitHub's link preview.
- Make the mock data agree with itself. The track list shows a PB of 1:42.345 while the best lap is 2:42.377.
- Optional: `docs/images/demo.gif`, a Bench Test lap on the timing and map screens.
- Dylan: one photo of Track Rat on the dash for the top of the README.

#### 7.3 Wiring diagram (Haiku, low)
**Status: done** (2026-10-25, 5a1e09f).
- `docs/images/wiring.svg`: the BE-252Q on the board's UART connector (5 V, GND, TX, RX, either way round), plus the optional button (IO21).

#### 7.4 Licences, credits and contributor files (Haiku, low)
**Status: done** (2026-10-25, 4902274 and c0127a0; the Wi-Fi page footer followed once the OBDb work landed).
- `THIRD_PARTY_LICENSES.md`: each library and its licence (from `.pio/libdeps/*/LICENSE`), the Arduino-ESP32 core, Inter (SIL OFL 1.1), DJI's GPS controller demo (MIT), and DJI's SDK terms.
- `DATA_LICENSE.md` (at the root, since `data/` becomes the board's file system): the Canadian tracks come from OpenStreetMap, © OpenStreetMap contributors, under the ODbL. The same credit goes in the README, on the install page, and in the Wi-Fi page's footer. The footer waits until the uncommitted Wi-Fi page work from another session lands.
- `CONTRIBUTING.md`, `.github/ISSUE_TEMPLATE/` (the bug form asks for the firmware version from Settings, the hardware, and a log from `serial_log.py`), `.github/pull_request_template.md`.
- `CHANGELOG.md` (Keep a Changelog): the 0.1.0-beta.1 entry, grouped by area, with what hasn't been tried on hardware yet (DJI camera, RaceChrono DIY profile, IMU, CAN).

#### 7.5 A one-screen README and docs/ (Sonnet, medium; after 7.1 to 7.4)
**Status: done** (2026-10-25, ef75001 and 7c15c21): a 100-line README; the rest moved word for word to docs/.
- The new README top, in order:
  - name and one line: an open-source GPS lap timer you build for about $50; offline, no app, no subscription;
  - badges: CI, latest release, licence, beta;
  - the photo or the gallery;
  - eight bullets on what it does;
  - **Build one:** parts with rough prices, and the wiring diagram;
  - **Install:** the installer first, then download or build from source;
  - **First drive:** five steps;
  - **Works with:** Circuit Tools, RaceRender, RaceChrono, Harry's LapTimer, TrackAddict, the DJI Osmo Action, Bluetooth OBD-II adapters;
  - **How accurate:** 20 Hz, crossings timed between fixes, about ±0.01–0.03 s per lap with open sky;
  - **Status:** beta, with a table of what's been tried on hardware;
  - links to the docs, the licence and the credits.
- Everything else moves to `docs/` with its wording unchanged, and every internal link keeps working:
  - `user-guide.md`
  - `hardware.md`, including the optional add-ons
  - `phone-and-video.md`
  - `flashing.md`
  - `development.md`: building, configuration, tests, layout, screens

#### 7.6 Ship it (Opus and Dylan)
**Status: released** (2026-10-25): [v0.1.0-beta.1](https://github.com/DylanD2402/track-rat/releases/tag/v0.1.0-beta.1), tagged at 780ef7d. Pages is on, the stale branches are deleted, and the installer is deployed by `installer.yml` (a tag can't deploy to github-pages, so it runs from main). The installer is at https://trackrat.ca/: the track-rat repo's custom domain, with HTTPS enforced. Dylan installed it from the web page onto the board on 2026-10-25, and it worked. Phase 7 is done.
- Dylan, once: GitHub → Settings → Pages → Source: **GitHub Actions**, and the repo's About description, website and topics.
- Tag `v0.1.0-beta.1` (annotated) and push it. Check the release, its files and the install page.
- Dylan: install from the web page, check Settings shows `v0.1.0-beta.1`, and drive.
- Delete the stale `claude/deep-dive-cleanup` and `claude/inter-ui-refresh` branches on GitHub (with Dylan's OK).

**Order:** 7.1 to 7.4 together (they touch different files), then 7.5, then 7.6.

### Phase 9: Menus that follow the day, and autocross as a first-class mode (proposed 2026-10-25)

**Why.**
- **The menu is a flat grid of modes:** Free Roam, New Track, Track List, then Performance, Settings, Wi-Fi.
- **Autocross is buried** under New Track → Course, next to building a circuit, which is rare.
- **Every event's course becomes a "track"** in the circuit list.
- **Runs are grouped by session,** so a power-off between runs splits the day.
- **A late or early tap on the finish** can only be fixed on the Wi-Fi page.

**What others do (researched):**
- **RaceBox** boots straight back into the last track nearby.
- **AiM Solo 2** offers CREATE beside the nearby tracks, and an open course can be kept "for the event duration only".
- **VBOX** sets a gate while driving, with a 5 s cancel.
- **RaceChrono** re-times every run when a trap is moved.
- **The Catalyst** is closed-loop only.
- **Nobody** lets you nudge a line precisely on the device itself.

#### The home screen: lead with what's next
```
+--------------------------------------------+
| Menu                         76%  ±1.4 m ▮▮|
| +----------------------------------------+ |
| | ► Autocross · Oct 25 course            | |   one tap: the most likely
| |   Run 4 next · best 52.31              | |   thing to do now
| +----------------------------------------+ |
|  [Track day] [Autocross] [Drag] [Drive]    |   the existing icons, reshuffled
|  Wi-Fi                          Settings   |
+--------------------------------------------+
```
- **Next up** (the big card) picks itself, in this order:
  1. today's autocross course nearby: its next run;
  2. a circuit within 5 km: "Calabogie · Full: start", with the PB;
  3. otherwise "Drive" (Free Roam; it still learns loops into tracks).
- **Track day:** nearby circuits by distance, as the track list does now, plus layouts, and **+ New circuit** last. Courses don't appear here.
- **Autocross:** the hub below.
- **Drag:** today's Performance (0–100, ¼ mile…).
- **Drive:** Free Roam.
- **Wi-Fi and Settings** become a small row. BOOT goes through them in this same order.

#### Autocross: courses are events, runs are numbered for the day
```
+--------------------------------------------+
| Autocross · Oct 25 course      Run 4 next  |
|  Best 52.31 (run 2)    Last 53.02 (run 3)  |
|  1  53.40    2  52.31 ★    3  53.02        |
| [Adjust start] [Adjust finish] [New course]|
+--------------------------------------------+
```
- **New course** (at the event):
  - Park in the grid or at the lights and press **Set start here**, while stopped. That's the standing start, as now.
  - On the first run, tap as you cross the finish. A 5 s **Undo** follows (as VBOX does), or set the finish later.
- **A course is an event:**
  - It's kept with its date and named "Oct 25 course" (rename it on the Wi-Fi page).
  - It lives in its own list, not among the circuits.
  - The hub shows today's course or the nearest recent one. Older courses stay on the card and on the Wi-Fi page.
- **The whole day is one run list:** Run 1, 2, 3…, whatever happens between runs (a power-off, a session ending, a restart).
- **Booting near today's course** opens it armed for the next run, so the second run takes no taps.
- **The debrief** compares the runs of the day: best run, theoretical best from the sectors, where time went.

#### Adjust the line precisely, on the device
```
+--------------------------------------------+
| Adjust finish                       Save   |
|   .. your runs' paths, zoomed to ~30 m ..  |
|          ====|====  ← the line              |
|  [◀ 0.5 m earlier]      [0.5 m later ▶]    |
|  Moved 1.5 m later                          |
|  Run 1 53.40→53.46   2 52.31→52.37  3 …    |
+--------------------------------------------+
```
- **Moving the line:** it slides along the driven paths in 0.5 m steps (hold for more).
  - That's about 20 ms at 90 km/h, so the line can be placed far more finely than a tap made while driving.
- **Live re-timing:** every run of the day re-times as you nudge, using the logs with the existing re-timing code.
- **Save** keeps the line for the rest of the event. **Adjust start** works the same way for rolling starts. A standing start is timed from moving off, so the start line matters less there.
- **Same edit on the Wi-Fi page:** the track editor gets the same nudge for courses.

#### Steps and who builds them
**Status: built** (2026-10-25): 9.2 436b328, 9.3a 4c9fbfe, 9.1 febf46e, 9.3b merged with them. Rendered and host-tested; not yet tried on the board.

| Wave | Step | Built by | Touches |
|---|---|---|---|
| 1 | 9.2 Courses as events: `"kind": "course"` stages with an event date, kept out of the circuit list; the day's runs numbered across sessions and power-offs (from the day's summaries); the Autocross hub (today's course, run list, best and last, New course, Adjust start/finish buttons wired later); New course = "Set start here" while parked; finish tap with a 5 s Undo; boot near today's course armed for the next run | Sonnet, medium | TrackDatabase, a new Courses module, main.cpp, a new hub screen, SessionLog, tests, renders |
| 1 | 9.3a Line nudge maths and the Wi-Fi page: an Arduino-free LineNudge.h (shift a line along its heading, each run's crossing time from the fixes near it, with tests); in the Wi-Fi page's track editor, ◀ ▶ 0.5 m nudge buttons for the start, finish and splits with each run's re-timed time shown live, then Save and re-time as today | Sonnet, medium | include/LineNudge.h, its test, web/index.html (track editor), test_web.js |
| 2 | 9.3b Adjust start/finish on the device: the screen sketched above. It loads the fixes near the line from each of the day's runs once, nudges with LineNudge.h, and Save stores the line and starts the existing re-time task for the course's logs | Sonnet, medium | a new screen, main.cpp, Courses |
| 2 | 9.1 The new home screen with Next up (pure choice logic, tested) and the Track day screen (circuits only, + New circuit last); the NEW TRACK chooser goes | Sonnet, medium | the menu drawing and touch areas, main.cpp, the track list |

The screens are rendered on the PC from the real display code (tools/render_screens.py) as each step lands. Dylan reviews them before anything is released.

Existing courses ("Course 2026-10-20" stages in tracks.json) move into the course list by themselves. Nothing else changes for circuits, laps or PBs.

### 0.1.1: clean-up, a matching website, and a code review (2026-10-25)

Beta 3 works on the board. Before adding more, 0.1.1 removes what isn't used, makes the website look like the device, and fixes the weak points of a full code review.

| Wave | Step | Built by |
|---|---|---|
| 1 | **Remove the LED and speaker features: done.** They weren't wired on Dylan's board and weren't planned, so the LED bar, the shift light, the board LED, the LED part of the overheat warning, voice notes and the planned beeps are gone from the code, settings, tests, docs and wiring diagram. The big button stays | Sonnet, medium |
| 1 | **The website looks like the device: done.** The Inter font, the device's colours and its rounded cards, as in the screenshots | Sonnet, low |
| 1 | **A code review in three parts: done.** Read-only reviews of security (Wi-Fi page, uploads, Bluetooth, the parsers), efficiency (the main loop, the card, memory), and reuse/clean-up (duplication, dead code, main.cpp's size). Findings and what 0.1.1 fixes: [docs/code-review-0.1.1.md](docs/code-review-0.1.1.md) | Sonnet, medium (×3) |
| 2 | **Screenshots that keep up with the screens: done.** Regenerate the README images, add the new screens (home, Autocross hub, Adjust), and a CI check that fails when docs/images is out of date | Sonnet, low |
| 2 | **Review fixes, storage: done.** Folder listing without per-file opens, recovery of the open session only, one summary parse, track maps kept in PSRAM, a PSRAM JSON allocator, `TimeFormat.h`, one `SafeFile` helper | Sonnet, medium |
| 2 | **Review fixes, main loop and screens: done.** Save the summary after the lap, sleep when idle, slower redraw of still screens, apply a new car's profile after the session, the Autocross hit zones, stale text, dead code | Sonnet, medium |
| 2 | **Review fixes, GPS and timing: done.** A bigger fix queue, remember the GPS pins, rate-limit the full reference search and the loop check, an I2C timeout, dead code | Sonnet, medium |
| 2 | **Review fixes, Wi-Fi page and the rest: done.** The network stays open, and each code works once; a temporary lockout, firmware checks, origin checks, upload limits, headers, the VBO reader's limits, the installer's tool hosted on trackrat.ca, Bluetooth that drops a phone that never subscribes, sanitizers in CI | Sonnet, medium |

Released as **v0.1.1-beta.1** (2026-10-26). Deferred findings (main.cpp's split, test helpers, the settings table, BLE bonding and others) are listed as Later in the review.
