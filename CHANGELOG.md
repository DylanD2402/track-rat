# Changelog

All notable changes to Track Rat are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the version numbers follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0-beta.2] - 2026-10-25

### Added

- trackrat.me: a homepage with menus, the firmware installer at /install/, and the analyser at /analyze/, which opens your session files in any browser (nothing is uploaded), with a simulated sample session.
- Drivers and cars on the device: up to 8 of each, chosen in Settings, with best laps kept for each pair, their names in the session files and on the Ready screen, the car picked by its VIN, and `/api/profiles` for the Wi-Fi page.
- Drivers and cars on the Wi-Fi page: add, rename, remove and choose them and set a car's VIN from the Settings tab, and see each session's driver and car in the session list.
- The Wi-Fi page and trackrat.me/analyze Compare tab show RPM, throttle, temperatures, g and car channels against distance, a friction circle, a corner sector table, and a shared cursor across the charts and the map.

### Fixed

- Compare (on the Wi-Fi page and trackrat.me/analyze) now opens Free Roam drives, stages and performance runs: a log with no laps is offered as one "Whole drive", and Free Roam drives have their own group in the track list.

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

[Unreleased]: https://github.com/DylanD2402/track-rat/compare/v0.1.0-beta.2...HEAD
[0.1.0-beta.2]: https://github.com/DylanD2402/track-rat/releases/tag/v0.1.0-beta.2
[0.1.0-beta.1]: https://github.com/DylanD2402/track-rat/releases/tag/v0.1.0-beta.1
