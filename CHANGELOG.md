# Changelog

All notable changes to Track Rat are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the version numbers follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- World tracks from the SD card: tiles at `/world/<lat>/<lon>.json` (2 degree squares, the format of `tracks.json`) are read at the first GPS fix and when you move into another tile, with the next tile too when within about 10 km of an edge. A world track that `tracks.json` also has, or within 1 km of one of your tracks, is hidden. Changing one (start/finish, map, corner sectors) copies it into `tracks.json` first. A start/finish marked `"estimated": true` is timed like any other, and the Ready screen and track list say "Start/finish estimated: check it after a lap" until you set the line. With no card or no `/world` folder nothing changes.
- A "World tracks" workflow builds the world tiles monthly and publishes `trackrat-world-tracks.zip` to the rolling `world-tracks` release; unzip it onto the SD card. A country that fails on every mirror is listed at the end; the run fails only if over 5% do.
- `tools/fetch_world_tracks.py` builds the world track set (Phase 10) from OpenStreetMap, country by country, as 2-degree tiles (`<lat>/<lon>.json`) plus `index.json`: English names with the local name kept, kart/dirt/drag rules as in the Canada tool, layouts for venues with several circuits, and an estimated start/finish (marked `"estimated": true`) for circuits with a mapped pit lane. Raw Overpass answers are cached, with retries and backoff. The code shared with the Canada tool is now `tools/osm_tracks.py`; the Canada tool works as before.

### Fixed

- The world tracks build no longer comes out without the United States or Canada: big countries (US, CA, RU, CN, AU, BR, IN, AR, MX, KZ) are fetched a state or province at a time, any country whose whole-country query fails falls back to that, and a build missing one of the main countries (US, CA, GB, DE, FR, IT, ES, JP, AU, BR, NL, BE, SE, PL) fails instead of publishing.
- Accented letters (track names like Autodrome La Chaudière, driver and car names) no longer drop out on the display: letters the fonts cannot draw are shown as plain ones (é as e, ß as ss), and curly quotes as straight ones.

## [0.1.1-beta.1] - 2026-10-26

Clean-up and hardening after a full code review ([docs/code-review-0.1.1.md](https://github.com/DylanD2402/track-rat/blob/main/docs/code-review-0.1.1.md)). Install both the firmware and the file system: the Wi-Fi page changed. Firmware older than this is refused by the Wi-Fi page and the SD card; use the web installer or USB for those.

### Changed

- The README screenshots show the current screens (start-up logo, home, Autocross hub, Adjust finish), and a CI job fails when they go stale (`tools/check_screens.py`).
- The main loop sleeps for 1 ms when nothing is waiting (no fix, no input, no frame due) instead of spinning, which saves battery.
- Screens with nothing moving on them (menus, settings, lists, Ready, summaries, the Autocross and Adjust screens, Wi-Fi, Performance cards) redraw only after a touch or press, a change, or every 350 ms, instead of 10 times a second. Timing, map, Free Roam, Performance live, the overheat warning, SYNC flash and the power-off bar keep their rate.
- A car found from its VIN is switched to only while the car is slow or there is no fix, so the flash write cannot land while driving (it already waited for the session to end).

### Removed

- The LED and speaker features, which were never wired or planned for the board: the WS2812 LED delta bar (`LED_BAR_*`), the board's own LED showing the delta (`BOARD_LED_*`), the shift light (`SHIFT_LIGHT_*`), the LED part of the overheat warning (the full-screen red and yellow card stays), voice notes (`VOICE_NOTES_ENABLED`) and the planned beeps. The big button stays. Old saved settings still load.

### Fixed

- Listing the sessions folder no longer slows down as it fills: the Autocross run list, Adjust start/finish, boot recovery and re-timing read the folder one entry at a time with FatFs instead of opening every file by path (which made it slower with every file), and take at most about 8 ms of a loop pass.
- Boot recovery only looks at the session that was open when the power went off (named in `/sessions/.open`) instead of re-checking every log on the card at every boot. The first boot after this update still checks them all once.
- Saving a summary (camera and sync marks) reads the old file once, not twice.
- Starting a session and checking the layout no longer parse all of tracks.json: every track's map is kept in memory when the file is loaded or saved.
- Small JSON documents are built in PSRAM, leaving internal memory to Bluetooth and Wi-Fi.
- Best-lap, stats, summary and tracks.json saves share one write-then-rename routine and the same recovery of a save cut short by a power cut; the best-lap file is only replaced once the new copy is whole and checked, and a half-written copy is never taken as the file.
- Lap time and delta text is written once (`TimeFormat.h`) with a test.
- GPS and timing speed-ups: the fix queue holds 256 fixes (12 s at 20 Hz, in PSRAM) instead of 64, so a long stall no longer drops the oldest ones; the GPS pins and baud rate are remembered and tried first, and each listen ends at the first valid data, so start-up finds the GPS sooner; off the reference lap (pit lane, out lap) the whole-lap search runs once a second, not on every fix; the loop finder repeats its out-and-back check once per pass; the I2C bus gives up after 5 ms instead of 50 ms when it's stuck. Lap times are unchanged.
- The session summary is saved a second after a lap instead of inside the lap event, so the lap flash and the fixes that follow are no longer delayed by the card. It is still saved at once when the session ends or the car parks, before the Wi-Fi page opens, and before sleep or power-off.
- The Autocross hub's buttons are touched where they are drawn: one shared set of rectangles replaces hit zones that were 2 px off. The Adjust screen's Back button while reading or saving uses its drawn row too.
- The user guide listed the Adjust screen's BOOT order wrongly (it goes Later, Cancel, Save, Earlier, Reset). Stale comments and the code layout list in the development guide are corrected.

### Security

- The Bluetooth (RaceBox/RaceChrono) side has no pairing, so it drops a phone that connects but doesn't subscribe to the data within 10 seconds, and goes on advertising. The no-pairing limit is noted in `docs/phone-and-video.md`.
- The web installer serves ESP Web Tools 10.4.0 itself (`site/install/esp-web-tools/`, Apache-2.0 licence beside it) instead of loading it from unpkg, so the page loads no third-party script.
- The VBO reader caps a header at 64 channels and 256 comment lines and a line at 2048 characters, rejects NaN and infinite values, and clamps values to their type before converting them, so a corrupt or hostile .vbo can't use unbounded memory or trigger undefined behaviour. Valid logs read as before.
- The 4-digit code works once. After every change that goes through (a setting, a driver or car, a note, a track edit, a `tracks.json` upload, a re-time, a firmware install, a restart), Track Rat draws a new code and shows it on the Wi-Fi screen at once, so a code someone saw on the network is already spent. The page forgets the code and asks for the new one ("That code is used up").
- Five wrong codes no longer lock changes until Wi-Fi is turned off: they lock for 30 s, doubling each time up to 8 minutes, and start over after a right code. A new code is drawn at each lockout, so guesses don't add up. The reply says how long to wait.
- The Settings tab collects changes (marked as changed, with a Not saved yet bar) and saves them in one request with one code; Save and restart saves and restarts with that same code (`restart=1`). All the settings in a request are applied, or none.
- Restart needs the code. A POST with an `Origin` that isn't the board's own page is refused (403), as is a chunked one (411).
- A firmware upload (and the SD card update) must be Track Rat's: the app's `project_name` has to be `trackrat` (set after the link by `tools/set_app_name.py`, since the prebuilt Arduino libraries say `arduino-lib-builder`). A new firmware is rolled back by the bootloader if it restarts before it has run for 30 s.
- A `tracks.json` upload is cut off at 2 MB (413), needs room on the card or in flash, and is checked as a whole file with a `tracks` list before it replaces the old one.
- Request bodies are refused by `Content-Length` before they are read: 2 MB for `tracks.json`, the app slot for firmware, 4 KB for everything else (413).
- At most two session downloads at once; a third gets 503, and the page waits and tries again.
- The page's replies carry `X-Frame-Options: DENY`, `X-Content-Type-Options: nosniff` and a `Content-Security-Policy` that allows only the page's own files, fonts and API.
- `/api/info` no longer includes the car's position. Reads stay open to anyone on the network.

## [0.1.0-beta.3] - 2026-10-25

### Added

- Adjust start and Adjust finish on the device: from the Autocross hub, slide a course's start or finish along the way the cars go in 0.5 m steps (hold to repeat; BOOT: hold picks a button, press uses it) and see the day's runs re-timed live from their logs, drawn over a zoomed view of the paths. Save writes the line and re-times the course's logs; Cancel changes nothing. Standing starts say how they are timed. The buttons are greyed out, with the reason, until there is a finish, a run and an SD card.
- The Wi-Fi page's track editor can nudge the start/finish, a stage's finish or a split in 0.5 m steps along the direction of travel (◀ ▶, with a reset), and shows each run's or lap's time now and with the moved line, timed again from the log live. Save and re-time work as before.
- Autocross courses are events: a New course is kept with the day it was made and named "Oct 25 course", and stays out of the circuit list. The new Autocross hub shows today's course, "Run 4 next", the best and last run and the day's run list, and starts runs or makes a new course while you're stopped at the start. Runs are numbered across the whole day, whatever happens between them, the first run's finish tap has a 5 second Undo, and a restart near today's course goes straight to timing, armed for the next run. Old "Course 2026-10-20" stages become courses by themselves.

### Changed

- The website moved to trackrat.ca, with the new logo and blue accent
- The new Track Rat logo on the board: the full TRACKRAT logo at start-up and the TR mark on the home screen, with the logo's blue (#26B7FF) as the highlight colour (selection rings, progress bars, the Bluetooth, OBD and camera tags). Timing colours keep their meanings.
- The analyzer at trackrat.ca/analyze has the same menu bar as the rest of the site, and the menu says Analyze.
- The menu is now a home screen that leads with what's next. A big **Next up** card picks itself and does the most likely thing in one tap: today's autocross course within 5 km (with "Run 4 next"), else a circuit within 5 km (with its PB), else Drive (Free Roam). Below it are four tiles, **Track day**, **Autocross**, **Drag** and **Drive**, then Wi-Fi and Settings. BOOT goes through them in that order. The circuits list is now Track day, with **+ New circuit** after the last track, and the NEW TRACK screen is gone: a new circuit starts there and a new course in the Autocross hub.

## [0.1.0-beta.2] - 2026-10-25

### Added

- trackrat.ca: a homepage with menus, the firmware installer at /install/, and the analyzer at /analyze/, which opens your session files in any browser (nothing is uploaded), with a simulated sample session.
- Drivers and cars on the device: up to 8 of each, chosen in Settings, with best laps kept for each pair, their names in the session files and on the Ready screen, the car picked by its VIN, and `/api/profiles` for the Wi-Fi page.
- Drivers and cars on the Wi-Fi page: add, rename, remove and choose them and set a car's VIN from the Settings tab, and see each session's driver and car in the session list.
- The Wi-Fi page and trackrat.ca/analyze Compare tab show RPM, throttle, temperatures, g and car channels against distance, a friction circle, a corner sector table, and a shared cursor across the charts and the map.

### Fixed

- Compare (on the Wi-Fi page and trackrat.ca/analyze) now opens Free Roam drives, stages and performance runs: a log with no laps is offered as one "Whole drive", and Free Roam drives have their own group in the track list.

## [0.1.0-beta.1] - 2026-10-25

First public beta.

### Timing

- Lap and split times from GPS timestamps, with the crossing interpolated between fixes.
- Automatic sessions: the pit debrief after a minute parked, and the next run is Session 2 when you come back later.
- Lap tags for pit laps, slow laps and suspect laps, and suspect laps never become a best.
- Direction-aware start/finish and split lines.
- Best-ever lap, personal bests and a live delta from the first lap of the day.
- Corner sectors, found automatically after the first clean lap, and theoretical best.
- A delta-first timing screen, with a map view and a ghost for the reference lap.
- Screens that follow the situation: slow, parked, and a session clock.
- Point-to-point courses for autocross, hill climbs and sprints.
- Acceleration and braking runs: 0-100 km/h, 1/4 mile and 100-0 braking.
- Karting: a smaller default gate, and a minimum lap time setting.

### Tracks

- A built-in list of 129 tracks: all 122 Canadian circuits in OpenStreetMap, with their town and province, and seven well-known circuits elsewhere (Laguna Seca, the Nürburgring GP, Silverstone, COTA, Spa, Monza and Suzuka). Plus the Bench Test, which runs a simulated car at your desk.
- Tracks that learn themselves from two loops of the same route.
- A track editor on the Wi-Fi page: drag the line, set its direction, add splits and name corners.
- Layouts: several layouts per venue, each with its own records.
- Re-timing: replay a saved session through the current timer.

### Sessions and logs

- Every GPS fix logged to the SD card in Racelogic VBO format, which Circuit Tools opens.
- Session summaries saved after every lap, and written safely so a power cut loses at most the last second.
- Unfinished sessions are rebuilt at boot.
- Logbook: a text note for each session.

### On the device

- Settings on the board, kept in flash.
- Battery percentage when a battery is fitted, sleep when parked, and a clean save on low battery.
- Power off from Settings or by holding BOOT, with the GPS in standby so it locks again quickly.
- A big button and an LED delta bar as optional add-ons.
- The GPS read as binary UBX-NAV-PVT messages, with real accuracy figures.
- An optional IMU (LSM6DSO) for real g-forces.
- Firmware update from the SD card, and the firmware version at the bottom of Settings.
- File dates on the SD card from the GPS clock, in your time zone.

### Wi-Fi page

- Turn it on in the pits, and it opens by itself on the phone. No app needed.
- Downloads of sessions as VBO, CSV, GPX and JSON.
- Lap comparison: speed and time gap against distance, and the map coloured by the faster lap.
- Season view: the best lap at each visit, and your PB over time.
- Track list, settings, and firmware updates.

### Phone apps and Bluetooth

- Bluetooth LE: a RaceBox Mini profile, or a RaceChrono profile with GPS and engine data.

### Engine data

- Engine data (RPM, throttle, coolant and intake temperatures) from a Bluetooth LE OBD-II adapter, tried with a Veepeak OBDCheck BLE+, with a shift light on the LED bar.
- Coolant temperature in the top bar, an overheat warning, and the coolant maximum in the summary.
- Oil temperature where the car supports it.
- CAN / OBD-II over the built-in CAN controller, with a transceiver.
- Car-specific data from an OBDb car profile on the SD card: brake pressure, steering angle and other values from the car's own modules, asked through the Bluetooth OBD-II adapter alongside the engine's, chosen by the car's VIN, logged as extra `.vbo` channels and CSV columns, and shown on the Engine data screen. `tools/obd_car.py` makes a profile from OBDb's signal sets.

### Video

- Track Rat as the GPS remote for a DJI Osmo Action: pairing, GPS into the video, and recording that follows the session.
- `tools/overlay.py`: overlay videos with the map, speed, lap times, delta, sectors, a g circle and engine bars.

### Tools

- `tools/serial_log.py`: capture the board's serial log, with `--reset`.
- `tools/render_screens.py`: draw the screens on a PC, without the board.
- `tools/fetch_canada_tracks.py` and `tools/fill_track_locations.py`: build the track list from OpenStreetMap.

### Not yet tried on hardware

- The DJI camera remote, with an Osmo Action 4.
- The RaceChrono DIY profile, with RaceChrono.
- The IMU.
- CAN / OBD-II, with a car and a transceiver.
- Oil temperature (only some cars report it).
- Car-specific data from OBDb car profiles, in a car.

Voice notes for the logbook aren't built yet; text notes work.

[Unreleased]: https://github.com/DylanD2402/track-rat/compare/v0.1.1-beta.1...HEAD
[0.1.1-beta.1]: https://github.com/DylanD2402/track-rat/releases/tag/v0.1.1-beta.1
[0.1.0-beta.3]: https://github.com/DylanD2402/track-rat/releases/tag/v0.1.0-beta.3
[0.1.0-beta.2]: https://github.com/DylanD2402/track-rat/releases/tag/v0.1.0-beta.2
[0.1.0-beta.1]: https://github.com/DylanD2402/track-rat/releases/tag/v0.1.0-beta.1
