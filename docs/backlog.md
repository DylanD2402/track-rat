# Backlog: what Track Rat needs next

A full list of what can be done, written on 2026-11-14 after reading the whole repo (firmware, tests, tools, the Wi-Fi page, the roadmap, both code reviews and the display plan) and researching the commercial timers. Nothing here is built. Pick the rows you want; each one says who it can be handed to. Back to the [README](../README.md).

The goal this list works towards: **one firmware that runs on any GPS module and any display, wired up and used, with the features of the AiM, Garmin Catalyst and RaceBox timers, a longer run time, and code that is easy to read and to debug.**

## How to read it

| Tag | Meaning |
|---|---|
| **Haiku** | Ready to build. The row names the files and what done looks like. Small, and mostly mechanical |
| **Opus** | Plan first. An Opus agent designs it and splits it into Haiku-sized briefs |
| **HW** | Not done until someone has tried it on a real board, car, camera or sensor |
| **Reverses** | Goes against an earlier decision in the [roadmap](../ROADMAP.md). Listed because the goal is parity; skip it if the decision still stands |
| S / M / L | Size, relative |

Timings and currents are estimates from reading the code, not measurements, unless a row says so.

With the agents in CLAUDE.md: an **Opus** row goes to the `planner`; a **Haiku** row that changes code goes to the `builder`, and one that only touches docs or images goes to `docs`. Roadmap numbers 1.x to 7.x and 9.x are in [ROADMAP-ARCHIVE.md](../ROADMAP-ARCHIVE.md); 8.x, 10 and 11 are in [ROADMAP.md](../ROADMAP.md).

**Rules for every agent brief** (the project's existing practice):

- Work in its own git worktree. Never flash the board or open a serial port. Commit on its branch; don't push.
- `pio run -e es3c28p -s` must build, and `python tools/run_tests.py` must pass. A firmware change also runs `python tools/build_sim.py` and commits `site/try/trackrat.wasm`.
- A change that shouldn't alter a screen must leave `tools/render_screens.py` output identical, pixel for pixel. A change that does alter one runs `tools/readme_images.py` and commits `docs/images`.
- New logic goes in a header with no Arduino code, with a host test, and the test's command goes in `.github/workflows/ci.yml` and `docs/development.md` (until RD-1 makes that one list).
- Update `CHANGELOG.md` under Unreleased, and the docs the change touches.

## What it needs next, in short

1. **Build the hardware layer.** The goal is a GPS- and display-agnostic firmware, and the plan for it exists ([display-plan.md](display-plan.md), review items A1 to A8), but none of it is built: there is no `include/boards/`, the GPS code is one class tied to a UART and to the u-blox M10's configuration messages, and the screens call TFT_eSPI directly. Three pieces unlock almost everything else: board files (HW-1), a GPS driver interface (GP-1) and the drawing interface (DS-3).
2. **Prove what's already there.** The DJI camera, the RaceChrono profile, the IMU, CAN, RaceBox mode with phone apps, the big button and the sleep current have never been tried on hardware. See [Needs the hardware](#needs-the-hardware).
3. **Make field bugs diagnosable before adding boards.** Today the only log is USB serial. There is no log on the card, no crash record, and no watchdog on the main loop. Every new board multiplies reports nobody can act on.
4. **Measure the power, then cut the big loads.** Every current in the code is an assumption (`BATTERY_DRAW_*` in `Config.h`). The likely big ones are the backlight at 100%, the CPU at 240 MHz and Bluetooth at full power.
5. **Then the parity features.** The gaps against the commercial timers are in the [table below](#how-it-compares).

## How it compares

From retailer and maker pages (see [Sources](#sources)); where they disagree, the row says so.

| | AiM Solo 2 DL | Garmin Catalyst 2 | RaceBox Mini / Mini S | Track Rat today | Gap |
|---|---|---|---|---|---|
| GPS rate | 10 or 25 Hz (sources differ) | 25 Hz, four systems | 25 Hz | 20 Hz, two systems (M10) | GP-7 |
| Tracks | 4,000+ | Preloaded | 1,500+ | About 11,000 from OpenStreetMap, most with an estimated line | EX-8 |
| Learns an unknown track | Yes | No | No | Yes | |
| Live delta, predicted lap | Yes | Yes | In the app | Yes | |
| Theoretical best | Yes | True Optimal Lap, as a video | In the app | Yes | |
| Coaching | No | Spoken, and top opportunities | No | Three corners with a hint each, on screen | FT-9, FT-14 |
| Shift and alarm lights | 10 RGB LEDs | No | No | Removed in 0.1.1 | FT-7, FT-8 |
| Pages you can set up | Yes | No | No | Three choices for the big number | FT-10 |
| Engine data | 1,000+ ECU protocols | No | No | OBD-II by Bluetooth, OBDb car profiles, CAN (untested) | DA-1, DA-9 |
| IMU | 100 Hz, six-axis | Yes | Yes | Optional add-on, untested | TR-3 |
| Video | SmartyCam link | Built-in 1440p camera | In the app | DJI GPS remote (untested), `overlay.py` | DA-12, EX-14 |
| Drag and performance | Yes | 0-60, 1/8, 1/4 mile | Drag meter | 0-100, 100-200, 1/8, 1/4, 100-0, rollout, slope flag | FT-16, FT-17 |
| Point to point, regularity | Yes | No | No | Stages and the Autocross hub | FT-18 |
| Past sessions on the device | Yes | Yes | No | The current session only | FT-1 |
| Phone and cloud | Wi-Fi to Race Studio 3 | App, paid cloud video, social | App | Wi-Fi page, analyzer in the browser | HW-13, EX-10 |
| Battery | Built in | Up to 45 min (one listing) | Over 20 h (Mini S) | Optional LiPo, run time unmeasured | Power section |
| Languages | Several | Several | Several | English | DS-23 |

Others worth matching: the **MyChron 6** (karts: RPM from the spark lead, two temperature inputs, 5 shift and 2 alarm LEDs, up to 20 h battery, best rolling lap) → DA-4, FT-2; the **Apex Pro** (LEDs showing how much of the car's grip is used, live view for the crew) → FT-14, DA-15; **Starlane** and **SpeedAngle** (motorcycles: live and peak lean angle) → FT-15; **VBOX Touch** (standing start with a separate finish, delta-V) → already there.

## Found while reading: fix these first

Small, real, and none needs a plan.

| ID | What | Detail | Hand to |
|---|---|---|---|
| FX-1 | **The IMU keeps running through deep sleep.** `Imu::begin()` sets the accelerometer and gyro to 104 Hz, and nothing ever powers them down | Add `Imu::sleep()` (write 0 to `CTRL1_XL` and `CTRL2_G`) in `src/Imu.cpp`; call it in `SleepControl::sleepNow()` before `Power::deepSleep()`. Done: the two register writes are in the sleep path and `test_imu.cpp` covers the register values. Roughly 0.5 mA saved asleep (datasheet figure, unmeasured) | Haiku · S · HW |
| FX-2 | **A temporary diagnostic is still in the Wi-Fi code.** `src/WifiPortal.cpp:67` says "Remove once the stall is understood"; the stall was fixed in db24dc1 | Remove `diag*` and its 3 s print, or fold what's useful into DB-5. Done: no "Wi-Fi diag:" line, build passes | Haiku · S |
| FX-3 | **Nothing watches the main loop.** A stuck loop (like the USB serial stall described in `setup()`) hangs until the battery is flat | See DB-4 | Haiku · S · HW |
| FX-4 | **An older u-blox GPS gets no configuration at all.** `GPSHandler::configureUblox()` only sends CFG-VALSET, which needs UBX protocol 27 or later (M9, M10, F9, F10). An M8 or older ignores it and stays at 1 Hz NMEA. The 0.2 review's A5 says the M8 "takes UBX already": it doesn't take this | See GP-2 | Haiku · M |
| FX-5 | **The test commands are written twice,** in `ci.yml` (54 steps) and `docs/development.md`, and a third time outside the repo in the queue's `run-ci-tests.sh`. `tools/run_tests.py` already reads its list from `ci.yml` | See RD-1 | Haiku · S |
| FX-6 | **`STATE_NAMES` in `main.cpp` must be kept in step with `enum class AppState` in `src/app/App.h` by hand** (19 entries each) | See RD-8 | Haiku · S |
| FX-7 | **The build has no warning flags.** The 0.2 review turned on `-Wall -Wextra` once by hand | See RD-3 | Haiku · S |
| FX-8 | **A release has no `.elf`,** so an address from a crash in the field can't be turned into a line of code | `tools/make_release.py` and `release.yml` attach `firmware.elf` (zipped). Done: the release dry run lists it | Haiku · S |
| FX-9 | **The roadmap says no small dual-band GPS manages 20 Hz.** The Quectel LG290P does (10 Hz by default, 20 Hz maximum), and the u-blox NEO-M9N does 25 Hz with four systems | Correct "Looked at, not worth it" in `ROADMAP.md`; see GP-10 | Haiku · S |
| FX-10 | **Carried from the 0.1.1 review, still open:** E12 (fixes read together after a stall share one arrival time), E15 (a long drive with no SD card needs one 1.6 MB block), E16 (a removed card isn't noticed), S11 (no Bluetooth bonding for the OBD adapter) | E12: time the on-screen clock from GPS time (Haiku · S). E15: write in a stream (Haiku · S). E16: see DB-20. S11: Opus · M | mixed |

## Any board

Review items A1, A4, A6, A7 and A8, plus what a wire-it-up firmware also needs.

| ID | What | Detail | Hand to |
|---|---|---|---|
| HW-1 | **Board files.** `include/boards/<board>.h` with the panel, its size, the touch chip and pins, the SD bus, the battery pin and divider, the buttons and the GPS pin pairs; `[env:<board>]` in `platformio.ini` picks one. `Config.h` keeps tuning only | Display plan step 1. Move, don't rewrite. Done: `es3c28p` builds to the same binary size ±1%, renders identical, CI builds every env | Haiku · M |
| HW-2 | **A second board at the same 320×240,** so the layer is proven before any layout scales. The M5Stack CoreS3 fits: ESP32-S3 with PSRAM, a 320×240 panel, FT6336 touch, SD on SPI, a power chip, an IMU (check the pinout and parts before buying) | Needs HW-1, HW-3, HW-4, HW-6 | Opus · M · HW |
| HW-3 | **SD card on SPI,** as well as 4-bit and 1-bit SDIO, and no card at all | `src/Storage.cpp`: the bus comes from the board file. `DirLister`'s FatFs path must work for both. Review A4 | Haiku · S · HW |
| HW-4 | **Battery back ends:** the ADC divider (today), a fuel gauge (MAX17048, BQ27220), a power chip (AXP2101), a charge-status pin where the board has one, and none | One interface under `Power.cpp`; `Battery.h` and `BatteryStats.h` stay for the voltage-only case. Review A6 | Opus · M · HW |
| HW-5 | **What a board can do, as flags:** touch, SD, battery, PSRAM, number of buttons, audio. Decide whether a board without PSRAM is supported (smaller buffers, direct draw) or refused | Today PSRAM holds the frame, the log buffer and the fix queue, each with a fallback of some kind | Opus · S |
| HW-6 | **IMU drivers behind one interface:** LSM6DSx (today), MPU6050/6500, ICM-42688, BMI270, QMI8658, found by their ID register | `ImuMath.h` is already chip-independent. One small driver per chip, each with a decode test like `test_imu.cpp` | Haiku · S each · HW |
| HW-7 | **Inputs from the board file:** one to four buttons, a rotary encoder. The events (`BootShort`, `BootLong`, taps) stay as they are | `src/InputManager.cpp`, `ButtonPress.h` | Haiku · S |
| HW-8 | **Add-on pins in `/config.json` on the SD card** (the big button, the IMU's bus, CAN), read at boot | No rebuild to move an add-on. Review A7. Needs HW-1 | Haiku · S |
| HW-9 | **CI and releases for every board:** a firmware per board in each release, and a board picker on the install page (one ESP Web Tools manifest per board) | `ci.yml`, `tools/make_release.py`, `site/install/` | Haiku · M |
| HW-10 | **A hand-wired board:** `boards/custom.h` as a commented template (an ESP32-S3 dev board, an SPI panel, any GPS), with a wiring table in `docs/hardware.md` | Needs HW-1 | Haiku · S |
| HW-11 | **"What a board must have" and "Add your board in six steps"** in `docs/hardware.md` | Review A8 | Haiku · S |
| HW-12 | **A "Hardware found" screen and `/api/info` section:** the GPS chip, its pins and rate, the touch chip, the SD bus and speed, the IMU, the battery back end | Needs GP-4 for the chip name. Makes a new build's first boot self-explaining | Haiku · S |
| HW-13 | **Join a Wi-Fi network** (home, or a phone's hotspot) as well as making one: back up sessions, check GitHub for new firmware, fetch track updates | The roadmap's "sync when home Wi-Fi is in range". Needs a password setting and care with the one-use code | Opus · L |
| HW-14 | **The SD card as a USB drive** when plugged into a PC | Needs TinyUSB mode, which changes how the serial log and the built-in JTAG work | Opus · M · HW |
| HW-15 | **Power from the car:** an ignition or USB-power sense pin in the board file, to wake with the car and save when it goes | Today USB power is only inferred | Haiku · S · HW |
| HW-16 | **Arduino core 3 (ESP-IDF 5)** through the `pioarduino` platform | Review F5. Needed for RGB panels and for automatic light sleep (PW-20). Some APIs change (`ledcSetup`, SD_MMC) | Opus · L · HW |
| HW-17 | **Other chips:** an ESP32-P4 with a C6 radio for MIPI panels; an ESP32-C6 as a screenless puck | Research only for now | Opus · S |

## Any GPS

Today `GPSHandler` (650 lines) is the UART, the pin search, the u-blox M10 configuration, the NMEA fallback, the fix queue and the simulated car, all in one class.

| ID | What | Detail | Hand to |
|---|---|---|---|
| GP-1 | **A GPS driver interface.** Split `GPSHandler` into a transport (UART, I2C, Bluetooth, the simulator, a log replay), a protocol driver (u-blox with VALSET, older u-blox, plain NMEA with a vendor's rate commands), and the fix queue the rest of the firmware reads. `GpsFix` doesn't change | The lap timer and screens only see `takeFix()`, `hasFix()` and a few status values already. The simulator's GPS stand-in (`tools/sim`) becomes one transport. Done: the same fixes from recorded bytes before and after (`test_gps_source.cpp`), and the simulator still builds | Opus · L |
| GP-2 | **Tell the u-blox generation and configure it properly.** Ask UBX-MON-VER for the protocol version. 27 and over: VALSET, as today. Under 27 (M8, M7): CFG-RATE, CFG-MSG, CFG-NAV5 and CFG-GNSS | Pure builders for the legacy messages in `UbxParser.h` with tests against the M8 protocol spec's examples; `GPSHandler::configureUblox()` picks. Fixes FX-4 | Haiku · M · HW |
| GP-3 | **Rate and baud commands for NMEA chips.** A pure `NmeaVendor.h` that builds the sentences with their checksums: MediaTek (`PMTK220`, `PMTK251`, `PMTK314`), Airoha and Quectel (`PAIR050`, `PAIR062`, `PAIR864`), CASIC as in the ATGM336H and L76K (`PCAS02`, `PCAS01`, `PCAS03`) | Review A5. Done: `test_nmeavendor.cpp` checks each sentence byte for byte. Sending them comes with GP-1 | Haiku · S |
| GP-4 | **Say which chip it is.** Read the start-up banner (`$GPTXT`, `$PMTK705`, `$PAIR020`) and MON-VER; show the name on the GPS Status screen, and write it into the `.vbo` header | Pure parser with a test | Haiku · S |
| GP-5 | **Read more NMEA:** GST (accuracy in metres, so the suspect-lap check works without u-blox), GSA (PDOP, fix type), any talker (`GN`, `GP`, `GA`, `GB`) | `GPSHandler::takeNmeaFix()`: fill `hAccM` and `vAccM` from GST instead of -1 | Haiku · S |
| GP-6 | **Raise the baud rate when the rate needs it.** 20 to 25 Hz of NMEA doesn't fit in 115200 with every sentence on | Needs GP-3 | Haiku · S · HW |
| GP-7 | **More GPS modes, from what the chip can do:** 25 Hz with GPS only on the M10, 25 Hz with four systems on an M9. A small table: chip → fastest rate for each number of systems | `SettingsData.h` (`GpsMode`), `settings::gpsSystems()`, the Wi-Fi page's field. Done: `test_settings.cpp` covers the new modes | Haiku · S · HW |
| GP-8 | **GPS on I2C** (u-blox at 0x42, Qwiic), for boards with no spare UART | A transport under GP-1 | Haiku · M · HW |
| GP-9 | **GPS by Bluetooth:** use a real RaceBox Mini, Mini S or Micro as the receiver (`RaceBoxPacket.h` already builds the packet; add the reader), and NMEA over a Bluetooth serial service | Lets a screen-only build work with a receiver someone already owns. Needs GP-1 and RD-7's Bluetooth helper | Opus · M · HW |
| GP-10 | **Dual-band and RTK-class receivers as plain receivers:** LG290P (20 Hz), ZED-F9P and X20P (25 Hz), LC29H (10 Hz), UM980. They speak UBX or NMEA, so GP-1 to GP-6 cover them. Corrections (NTRIP through a phone) are a separate question | List the tried ones in `docs/hardware.md` | Opus · S · HW |
| GP-11 | **A signal screen:** a bar for each satellite, by system (UBX-NAV-SAT, or GSV), and a jamming flag | Helps place the antenna; every commercial unit has one | Haiku · M |
| GP-12 | **Standby for chips that aren't u-blox:** the vendor's own command (`PMTK161`, `PAIR650`), or a power pin from the board file | `GPSHandler::standby()` is u-blox only | Haiku · S · HW |
| GP-13 | **Smooth over dropouts.** Blend the IMU and the GPS (a Kalman filter) to carry position through a bridge or trees, and to give 50 to 100 positions a second | The Catalyst does this. Needs a working IMU (TR-3) | Opus · L · HW |
| GP-14 | **Reject a bad date** from a cheap module (a week rollover gives a year in the past): refuse dates before the firmware's build date | `GpsSource.cpp`, `takeNmeaFix()` with a test | Haiku · S |

## Any display

Steps 1 to 9 of [display-plan.md](display-plan.md) stand as written; they are DS-1 to DS-9 here so they can be picked like the rest. DS-10 on are new.

| ID | What | Detail | Hand to |
|---|---|---|---|
| DS-1 | Board files | The same work as HW-1 | |
| DS-2 | **`UiAction`s and one BOOT-button convention** (plan step 2, what's left of it; review C8). Each screen turns a touch into Back, Next, Select, Item(n) or Hold from the rectangles it drew | 23 places handle BOOT today, and Adjust's order is the reverse of the others | Opus · M |
| DS-3 | **`gfx::Canvas`** with the TFT_eSPI back end, and the PC stand-in as a Canvas (plan step 3) | Done: every render identical | Opus · M |
| DS-4 | **The top bar's status passed in as a model** (plan step 4): battery, GPS, BT, OBD, camera, coolant | `DisplayManager::header()` reads `Settings`, `EngineData`, `RaceBoxBle` and `CameraBle` itself today | Haiku · S |
| DS-5 | **`ui::Metrics` and a layout function for every screen** (plan step 5), rendered at 480×320 and 240×320 in CI with a check for text off the screen or outside its button | The largest piece: about 320 drawing calls with literal positions | Opus · L, then Haiku per screen |
| DS-6 | **Font sets per scale** (plan step 6): `tools/make_fonts.py --scale` | | Haiku · S |
| DS-7 | **LovyanGFX back end and a larger board** (plan step 7). The Guition JC3248W535 (3.5", 320×480, QSPI, ESP32-S3 N16R8, battery charger) is the popular one now | | Opus · M · HW |
| DS-8 | **DMA and the RGB framebuffer path** for 800×480 boards (plan step 8): the Sunton 8048S043/050/070 and Waveshare's 4.3", 5" and 7" | Board revisions differ in pins and init code; check before buying | Opus · M · HW |
| DS-9 | **LVGL renderer** (plan step 9, optional) | | Opus · XL |
| DS-10 | **Touch drivers behind one interface:** FT6336 (today, registers read in `InputManager.cpp`), GT911, CST816 and CST820, the resistive XPT2046, and none. Found by probing I2C at boot | Review A2 | Opus · S, then Haiku per chip · HW |
| DS-11 | **Touch orientation as a first-boot step** ("tap the corners") instead of `TOUCH_SWAP_XY` and `TOUCH_INVERT_X/Y` | Review A3. Needs DS-10 | Haiku · S |
| DS-12 | **One firmware, the panel chosen at run time.** LovyanGFX can be set up from data, so a board profile (in flash, written by the web installer or chosen at first boot) could pick the panel, bus and pins with no rebuild | This is what makes "wire it up and use it" true for the screen. Needs DS-7 | Opus · L |
| DS-13 | **No screen at all:** a puck on any ESP32-S3 board that logs, streams over Bluetooth and serves the Wi-Fi page, with one LED for its state | A null Canvas. The RaceBox Micro's role, for the price of a GPS module and a dev board | Opus · S |
| DS-14 | **Small one-colour panels:** a 128×64 OLED, and the Sharp memory LCD (400×240, readable in full sun, microamps) | A 1-bit Canvas and its own compact layout: lap time, delta, speed. The roadmap calls the 230-nit panel hard to read in sun | Opus · M · HW |
| DS-15 | **AMOLED panels** (QSPI: the LilyGo T-Display S3 AMOLED, Waveshare's 2.41") | True black suits the theme. After DS-7 | Opus · S · HW |
| DS-16 | **A second, remote display:** a small panel on the wheel showing the delta, fed by the main unit over ESP-NOW or Bluetooth | | Opus · M · HW |
| DS-17 | **A bright theme for sun:** white background, black digits, as a setting | `UiTheme.h`'s colours are compile-time constants; they'd become a palette chosen at run time | Opus · S |
| DS-18 | **Dim by itself at night:** from the sun's height (worked out from GPS time and position, no sensor), or a light sensor where the board has one | Pure function with a test, then `applySettings()` | Haiku · S |
| DS-19 | **All screen text in one table,** then French | The text is literals across `Display*.cpp`. The table is also what LVGL would need. Fonts already fold accents; a French set needs the accented glyphs made | Opus · M, then Haiku |
| DS-20 | **A QR code on the Wi-Fi screen** that joins the network and opens the page | A small QR encoder (pure, tested) drawn with `fillRect` | Haiku · S |

## Features

### On the device

| ID | What | Detail | Hand to |
|---|---|---|---|
| FT-1 | **Past sessions on the device:** a list by day and track with each best lap; open one to see its laps and debrief | Today only the current session shows. Read the summaries a few per loop pass, as `CourseRuns` does | Opus · M |
| FT-2 | **Best rolling lap:** the quickest full lap starting anywhere, not only at the line | Pure maths on the lap traces, with a test. In the summary and the analyzer | Haiku · S |
| FT-3 | **A target lap:** type a time, and the delta is against it. For noise limits, fuel saving and endurance pace | `CompareTo` gets a fourth choice | Haiku · S |
| FT-4 | **Pit lane speed limit and pit stop timer.** In the pit lane (the map's `pitLines`), the slow screen turns red over the limit; the stop is timed from pit-in to pit-out and standing still | `SituationScreen.h` already finds the pit lane. A setting for the limit | Haiku · M |
| FT-5 | **Endurance pack:** a stint timer with a warning before the longest stint, a driver change at the pit stop, laps of fuel left | Fuel needs a level or flow PID (DA-9) or a typed amount | Opus · M |
| FT-6 | **Gear,** worked out from RPM over speed once each gear has been seen | The 6.1 stretch goal. Pure, tested; a log channel | Haiku · S |
| FT-7 | **RPM bar and shift flash on the screen:** a bar along the top, and the screen edge flashes at the shift point | A setting for the shift RPM. No LEDs needed | Haiku · S |
| FT-8 | **LED strip:** shift lights, a delta bar, alarms | The code was removed in 0.1.1 and is in git history. AiM, MyChron and Apex Pro all lead with LEDs. As a board-file add-on | Haiku · S · HW · Reverses |
| FT-9 | **Sound:** a beep at the line, tones for gaining and losing, the pit limit, overheating; then spoken lap times from stored clips | Dropped in 0.1.1. The board has an ES8311 codec that was never brought up; a piezo on a pin is the cheap route. Bluetooth audio stays impossible on this chip | Opus · M · HW · Reverses |
| FT-10 | **Choose what the timing screen shows:** two to four slots from a list (delta, predicted, last, best, speed, RPM, coolant, gear, session clock) | "Not planned" in the roadmap. Far easier once DS-5's layout functions exist | Opus · M · Reverses |
| FT-11 | **A general alarm:** any channel with a low or high limit gives the full-screen warning (oil pressure, battery voltage, boost, fuel) | Generalise `TempAlert.h` | Opus · S |
| FT-12 | **Laps so far, while parked in a session:** tap the last-lap box for the last few laps with their sector colours | | Haiku · S |
| FT-13 | **Consistency:** the spread of the clean laps and of each sector, in the summary and the analyzer | Pure, tested | Haiku · S |
| FT-14 | **A grip meter:** learn the car's g-g envelope, then show how much of it is being used, and name the corners with grip left over | The Apex Pro's whole idea. Needs real g (TR-3) | Opus · L · HW |
| FT-15 | **Motorcycles:** live and peak lean angle left and right, from speed and yaw rate | Pure maths with a test, then a screen field and a log channel. Needs the IMU | Opus · S, then Haiku · HW |
| FT-16 | **Drag tree:** staging, ambers or a pro tree, reaction time, 60 ft, a red light | Roadmap 8.3, not built | Opus · S, then Haiku |
| FT-17 | **More performance runs:** your own speed ranges (60-130 mph, 80-120 km/h), 0-100-0, half mile, standing kilometre, braking from any speed, peak g | `PerfRun.h` already has milestones | Haiku · M |
| FT-18 | **Regularity:** a target average speed for a stage, and seconds ahead or behind | AiM's Regularity mode | Opus · S, then Haiku |
| FT-19 | **Rally days:** several stages in an event, the total, the order | Extends courses | Opus · S |
| FT-20 | **Someone else's lap as the reference:** load a friend's or coach's `.vbo` on the Wi-Fi page | Roadmap 8.6, not built | Opus · S, then Haiku |
| FT-21 | **Conditions quick-picks:** dry, damp or wet; tyres; temperature; saved with the session | Roadmap 8.7, not built | Haiku · S |
| FT-22 | **Each car's distance, running hours and tyre sets,** with heat cycles counted | `Profiles.h` plus the session lengths. The MyChron's engine hours | Haiku · M |
| FT-23 | **Units one by one:** temperature, pressure and distance apart from speed | Today °F comes with mph | Haiku · S |
| FT-24 | **Skidpad:** steady cornering g on a circle, in Performance | | Haiku · S |
| FT-25 | **A Wi-Fi password, as a setting** | The network is open by the owner's choice (review S1); some will want it closed | Haiku · S |

### Sensors and connections

| ID | What | Detail | Hand to |
|---|---|---|---|
| DA-1 | **Listen to the car's CAN bus** instead of asking: the frames cars send by themselves, 50 to 100 times a second, decoded from a per-car definition file (steering, brake, wheel speeds, throttle) | How AiM gets fast data. Needs the transceiver (TR-4) and a source of definitions with a licence that fits | Opus · L · HW |
| DA-2 | **Track Rat as a GPS and IMU for a dash:** its fixes on CAN or a UART, in a format dashes read | VBOX Micro and SPEEDBOX do this | Opus · S, then Haiku · HW |
| DA-3 | **Analog and pulse inputs:** an ADS1115 for 0-5 V sensors (brake pressure, steering, throttle, oil pressure, wideband), pulse inputs for wheel speed and RPM; a file on the card names each channel and its scaling | Each becomes a `.vbo` channel, as the OBDb car channels do | Opus · M · HW |
| DA-4 | **Kart pack:** RPM from a clip on the spark lead, a thermocouple for exhaust or cylinder head (MAX31855), water temperature | MyChron parity. Builds on DA-3 | Opus · M · HW |
| DA-5 | **Tyre pressures** from Bluetooth valve caps | Roadmap 8.11, not built. Formats differ by brand | Opus · M · HW |
| DA-6 | **Heart rate** from a Bluetooth strap (service 0x180D) | Roadmap 8.9, not built | Haiku · S · HW |
| DA-7 | **Tyre temperatures** from infrared sensors at each wheel | | Opus · M · HW |
| DA-8 | **A barometer** (BMP390, BME280): height and slope better than GPS, air temperature and density altitude for drag runs | Driver: Haiku. Use in `PerfRun`: Opus | mixed · HW |
| DA-9 | **More OBD-II values:** fuel level, manifold pressure, lambda, battery voltage, speed; the car profile lists which to ask | `Obd.h`'s table and schedule | Haiku · S |
| DA-10 | **Read fault codes** (never clear them) on the Engine data screen | | Haiku · S · HW |
| DA-11 | **A third Bluetooth profile: plain NMEA** over a serial service, for apps that don't know RaceBox or RaceChrono | A pure sentence builder with a test, beside `RaceChrono.h` | Haiku · M · HW |
| DA-12 | **GoPro HERO9 and later:** recording with the sessions (Open GoPro) | Roadmap 8.10, not built | Opus · S · HW |
| DA-13 | **A Bluetooth button** for the SYNC flash, a mark or ending the session | Roadmap 8.12, not built | Opus · S · HW |
| DA-14 | **A lap beacon input** (infrared or a magnetic strip) for indoor karting, where there's no GPS | One digital input feeding `LapTimer` | Opus · S · HW |
| DA-15 | **Live view for the crew** over a LoRa radio or a phone's hotspot | "Not worth it" in the roadmap because Wi-Fi won't reach; LoRa does | Opus · M · HW · Reverses |

### Files, analysis and sharing

| ID | What | Detail | Hand to |
|---|---|---|---|
| EX-1 | **More export formats,** made in the browser like the CSV: RaceRender CSV, Telemetry Overlay CSV, GPX with speed, KML; then MoTeC i2 (`.ld`) | `web/index.html`, `test/test_web.js`. `.ld` is binary: Opus plans that one | Haiku · S each |
| EX-2 | **Open other loggers' files** in the analyzer: VBOX and RaceChrono `.vbo`, RaceBox CSV, AiM CSV | Also feeds FT-20 | Haiku · S each |
| EX-3 | **A picture to share:** the map, the best lap, the date | Roadmap "smaller ideas" | Haiku · S |
| EX-4 | **A printable session report** | A print stylesheet | Haiku · S |
| EX-5 | **A whole day as one zip,** and downloads compressed | Less time on Wi-Fi, which is the hungriest mode | Opus · S |
| EX-6 | **Analyzer views:** the map coloured by any channel, more than two laps at once, a lap-by-sector table, histograms, your own maths channels | One row each for Haiku once RD-6 has split the page | Haiku · S each |
| EX-7 | **Share a learned track:** export and import one track | Roadmap 8.8, not built | Haiku · S |
| EX-8 | **Tracks checked by people:** a place to send back a corrected start/finish line and corner names, so an estimated line is fixed for everyone | 11,000 tracks, most with a line guessed from the pit lane | Opus · M |
| EX-9 | **Update the Wi-Fi page and track list with no PC** | Roadmap 8.13, not built | Haiku · M |
| EX-10 | **A club board** on trackrat.ca from summaries people choose to upload | "Not planned" (cloud and leaderboards) | Opus · M · Reverses |
| EX-11 | **Satellite picture under the track editor** on trackrat.ca/analyze | Roadmap "smaller ideas" | Haiku · S |
| EX-12 | **The analyzer installed on the phone,** working offline | A service worker and a manifest | Haiku · S |
| EX-13 | **Talk to the device from trackrat.ca over Bluetooth** (Chrome on Android and desktop): settings and a live view without joining its Wi-Fi | | Opus · M |
| EX-14 | **Video and data together in the browser:** drop a video and a log, see them play in step, export the overlay | Would replace `overlay.py` for most people | Opus · L |

## Longer run time

Measure first: PW-1. The estimates below are from data sheets and the code.

| ID | What | Detail | Hand to |
|---|---|---|---|
| PW-1 | **Measure every mode** with a USB power meter: menu at 100%, 50% and 10% brightness; timing with logging; Bluetooth on and off; the Wi-Fi page; screen off; deep sleep. Put the table in `docs/hardware.md` and correct `BATTERY_DRAW_*` | No agent can do this. A `tools/power_log.py` that labels the modes over serial is Haiku · S | HW |
| PW-2 | **Dim when parked.** Not timing and untouched for a minute: 30% brightness; full again on a tap or on moving. And start new units below 100% | The backlight is probably the largest single load. `SleepTimer.h` gets a Dim step before ScreenOff; a test in `test_power.cpp` | Haiku · S |
| PW-3 | **How long before sleeping, as a setting:** 2, 5, 10 or 20 minutes, or never | `AUTO_SLEEP_IDLE_MS` is fixed at 20 minutes | Haiku · S |
| PW-4 | **A slower CPU clock where nothing is live:** 160 MHz while timing, 80 MHz on menus and with the screen off. Review F4 | One call in `enterState()`. The radios need 80 MHz, and at 80 and over the UART, SPI and backlight timing don't change. The review guessed 10 to 20 mA | Haiku · S · HW |
| PW-5 | **Bluetooth at lower power.** It runs at the maximum (+9 dBm, `RaceBoxBle.cpp:132`) for a phone a metre away; and advertise more slowly after two minutes with no phone | | Haiku · S · HW |
| PW-6 | **Stop looking for a device that isn't there.** A remembered OBD adapter or camera is looked for every 10 s for ever. After five minutes without it: once a minute, and quickly again after a touch or driving off | `src/ObdBle.cpp:23` and the same in `CameraBle.cpp`. Matters most for a unit moved to a kart | Haiku · S |
| PW-7 | **A slower GPS when parked outside a session:** 1 to 5 Hz after a few minutes parked on a menu, full rate on moving or on the Ready screen | A rate change is one VALSET in RAM and doesn't restart tracking. Fewer fixes also means less work for the CPU | Haiku · S · HW |
| PW-8 | **Read the touch chip only when it has something.** Its interrupt pin is wired (IO17) but the chip is read over I2C 50 times a second regardless | `InputManager::poll()`: skip the read while the pin is high and no touch is in progress. A small saving; it also leaves the bus to the IMU | Haiku · S · HW |
| PW-9 | **Read the IMU in batches:** its FIFO once per fix instead of a status read every 4 ms | `src/Imu.cpp` | Haiku · S · HW |
| PW-10 | **Put the panel to sleep whenever the backlight is off,** not only before deep sleep | `DisplayManager::setBacklight()`, `powerDown()` | Haiku · S · HW |
| PW-11 | **Screen off while timing,** as a mode: the screen lights for five seconds at the line, or stays off and only logs | For karts and long stints on a battery | Haiku · M |
| PW-12 | **Wi-Fi at lower power,** and off by itself after some minutes with no phone joined | The phone is beside the board | Haiku · S · HW |
| PW-13 | **Don't draw a still screen that hasn't changed.** Still screens redraw every 350 ms whatever happens | Compare what the screen is given with last time; skip the clear, draw and compare when it's the same | Haiku · M |
| PW-14 | **Know what changed while drawing** (review F2), and **DMA** (review F1) | Two passes over 150 KB of PSRAM per frame today, and the loop waits about 50 ms for a full send | Opus · M |
| PW-15 | **Fewer temporary Strings when drawing** (review F3) | `text()`, `fit()` and their callers | Haiku · S per file |
| PW-16 | **Sleep until something happens.** The loop wakes every millisecond to look. Wait on the UART, the input pins and the frame timer instead | | Opus · S |
| PW-17 | **Kinder to the card:** reserve the log file's space ahead, write in whole clusters, sync less often, and close the card before sleeping | Also shortens the slowest write. Measure card current first | Opus · S · HW |
| PW-18 | **Find what draws current asleep:** the IMU (FX-1), the card, the touch chip, the audio codec, the regulators | The sleep current has never been measured (roadmap 1.6) | Opus · S · HW |
| PW-19 | **Wake when the car moves:** the IMU's motion interrupt on a wake pin, so the unit can sleep after two minutes in the paddock and be ready by the pit exit | The biggest gain for a day on one battery. Needs one more wire to the IMU | Opus · M · HW |
| PW-20 | **Automatic light sleep and clock scaling** | The stock Arduino libraries appear to be built without power management (unconfirmed; check their sdkconfig). Comes with HW-16 | Opus · M · HW |

## Code that's easier to read

| ID | What | Detail | Hand to |
|---|---|---|---|
| RD-1 | **One list of tests.** Partly done: `tools/run_tests.py` runs every test from the list in `ci.yml`, on Windows too. Left: `development.md` describes each test without repeating its command, and the queue's `run-ci-tests.sh` calls the runner | Review C14, the part left. Done: no `g++` test command in `development.md` | Haiku · S |
| RD-2 | **`.clang-format`** matching today's style (2 spaces, about 120 columns), checked on changed lines only | Review C15 | Haiku · S |
| RD-3 | **`-Wall -Wextra` on the project's own sources** (`build_src_flags`), an `.editorconfig`, and warnings as errors in CI only | | Haiku · S |
| RD-4 | **Folders.** `include/` is 95 headers in one flat list. Group them: `core/` (timing, sessions, parsers: no Arduino), `hal/` (GPS, display, touch, storage, power), `ble/`, `ui/`, `app/` | Every include path and test command changes, so do it when nothing else is in flight | Opus · S to schedule, then Haiku |
| RD-5 | **Split the largest files,** moved line for line and checked the way the `main.cpp` split was (a script comparing what was removed with what was added): `WifiPortal.cpp` (1,636 lines: sessions, files, tracks, firmware, settings), `DisplayManager.cpp` (1,256), `TrackDatabase.cpp` (1,139), `Session.cpp` (1,066), `SessionLog.cpp` (923) | One file per agent | Haiku · M each |
| RD-6 | **The Wi-Fi page as modules.** `web/index.html` is 3,201 lines in one file. Split the script into modules (the log reader, laps, charts, the track editor, files, settings) that `tools/build_web.py` joins; `test_web.js` imports them | | Opus · S, then Haiku |
| RD-7 | **The review's open clean-ups:** C7 one `button()` (Haiku · S); C8 one BOOT convention (with DS-2); C10 `SessionName.h` (Haiku · M); C11 one golden `.vbo` for the three readers (Haiku · M); C12 one table describing every setting, driving flash, the device list, the Wi-Fi names and the tests (Opus · L); C16 one Bluetooth-central helper for the adapter and the camera (Opus · M) | [code-review-0.2.md](code-review-0.2.md) | mixed |
| RD-8 | **State names from the enum:** a `name(AppState)` with a `switch`, so the compiler says when one is missing | `src/main.cpp:61`, `src/app/App.h` | Haiku · S |
| RD-9 | **One shape for every screen:** enter, leave, input, fix, draw, still. `main.cpp`'s three `switch` statements become one table | | Opus · S, then Haiku |
| RD-10 | **Pass the shared objects in** instead of the `extern`s in `App.h` | What lets the whole app build on a PC (DB-12) | Opus · M |
| RD-11 | **`docs/architecture.md`:** how a fix travels (UART → queue → lap timer → log ring → card; → screen), which task runs on which core, what lives in PSRAM, the locking rules, and where to add a new screen, setting, channel or board | | Haiku · S |
| RD-12 | **Long logic headers into `.h` and `.cpp`:** `Elm327.h` (735 lines), `AdjustLine.h` (648), `SettingsFlow.h` (558), `PortalLogic.h` (547), `SettingsData.h` (534), `PerfRun.h` (504) | Still pure C++ and host-tested; faster builds, shorter headers | Haiku · S each |
| RD-13 | **`Config.h` in parts:** the board (HW-1), tuning, new-unit defaults, feature switches | | Haiku · S |
| RD-14 | **A word list** in `CONTRIBUTING.md`: lap, run, session, stage, course, event, layout, venue, split, sector, gate | The code uses each precisely; a newcomer has to work it out | Haiku · S |
| RD-15 | **The Python tools:** one shared `.vbo` reader, `ruff` in CI | `overlay.py` and `make_sample_session.py` each have their own | Haiku · S |
| RD-16 | **The unused-name scan as a script** in `tools/`, run in CI as a report | The 0.2 review did it once by hand | Haiku · M |

## Code that's easier to debug

| ID | What | Detail | Hand to |
|---|---|---|---|
| DB-1 | **A log with levels and tags:** `LOGI("gps", ...)`, a time stamp on each line, the level changed per tag while running. `DEBUG_PRINTF` becomes a shim first, so nothing else changes | `include/Log.h`, pure where it can be | Haiku · S |
| DB-2 | **The log on the SD card:** `/logs/<date>_<time>.log` through the log writer's task, the last few megabytes kept; Download device log on the Wi-Fi page. The bug form asks for it | The only log today needs a PC on USB, which nobody has at a track | Opus · S, then Haiku |
| DB-3 | **A record of every crash.** At boot, read the reset reason and the core dump's summary (the partition table already has a `coredump` slot); write `/logs/crash-<time>.txt`; say so once on screen. `tools/decode_crash.py` turns the addresses into lines with FX-8's `.elf` | Check that the stock libraries save core dumps to flash | Opus · S, then Haiku · HW |
| DB-4 | **A watchdog on the main loop** (8 to 10 s), fed by `loop()` and by the long waits in `setup()`. A hang becomes a restart with a crash record | Fixes FX-3 | Haiku · S · HW |
| DB-5 | **Health in one place:** the longest and average loop pass, frame time, the fix queue's high-water mark, dropped fixes, the slowest card write, I2C errors, free memory (internal and PSRAM, smallest ever, largest block), each task's free stack, Bluetooth drop reasons, the reset reason, boot count. Printed every 5 s, saved in each session summary, served at `/api/diag`, shown on Settings > Diagnostics | One struct. After a session it says whether any fix was lost | Haiku · M |
| DB-6 | **Serial commands** as lines: `help`, `status`, `heap`, `tasks`, `log <tag> <level>`, `gps raw on`, `sim circuit`, `replay <file>`, `set <key> <value>`, `screenshot`, `reboot`. The `b`, `p` and `r` keys stay | `Power.cpp` reads the serial port today, of all places; move it to its own file | Haiku · M |
| DB-7 | **A screenshot from the real panel:** `GET /api/screenshot` as a BMP of the frame, and `tools/grab_screen.py` | For bug reports, and docs pictures from hardware | Haiku · S |
| DB-8 | **Replay a real session on the device,** in real time, as if it were the GPS | The roadmap's own second habit, never built: the Bench Test still drives a circle. With GP-1 it's one more source | Haiku · M |
| DB-9 | **Save the raw GPS bytes** to `/logs/gps-<time>.ubx` when asked | Opens in u-center and replays through `test_gps_source.cpp`. Needed for every "it doesn't work with my module" report | Haiku · S |
| DB-10 | **Save the OBD and camera conversations** at debug level | An untried adapter's log becomes a test, as the Veepeak's did | Haiku · S |
| DB-11 | **Say which part of a slow loop pass was slow:** timers round GPS, session, input, draw and Wi-Fi; a pass over 50 ms logs its parts | | Haiku · S |
| DB-12 | **The whole app on a PC.** Partly done: the Try it page (`site/try`, `tools/build_sim.py`) runs the firmware unchanged as WebAssembly, with stand-ins for the hardware in `tools/sim/hal`, and `tools/sim/smoke.mjs` drives it in CI. Left: a `.vbo` as a GPS source (it has the simulated car, the phone's location and off), and a native desktop build of the same stand-ins to step through with a debugger | The stand-ins are a working hardware seam already: HW-1, GP-1 and DS-3 should build on them, not beside them | Opus · M |
| DB-13 | **The last 64 events** (state changes, laps, errors) kept in RTC memory and written into the crash record | Needs DB-3 | Haiku · S |
| DB-14 | **Step through the code on the board.** The ESP32-S3's built-in USB JTAG is the port already in use: a debug env in `platformio.ini`, `.vscode/launch.json`, and a page in `development.md` | | Haiku · S · HW |
| DB-15 | **More from CI:** static analysis (`pio check`); firmware size and static RAM for each pull request, with a limit; test coverage; short fuzz runs of every parser (UBX, NMEA, VBO, ELM327, DNS, car profiles, the RaceChrono filter) | | Haiku · S each |
| DB-16 | **A self-test screen:** a touch grid, colour bars, card speed, GPS bytes a second, the battery reading, an I2C scan, each button. Also over serial as JSON | Replaces most of `TESTING.md`'s steps, and is how a new board is checked | Opus · S, then Haiku |
| DB-17 | **Checks that tell you:** a `TR_CHECK(cond)` that logs the file and line and counts in DB-5; stops in a debug build only | | Haiku · S |
| DB-18 | **Two builds:** release (info level), and debug (no optimisation, the core's own debug output, checks that stop, DB-11 on) | `platformio.ini` | Haiku · S |
| DB-19 | **Tell the driver when something is wrong:** a mark in the top bar when the log isn't being written, the card is full or gone (E16), fixes were dropped or the GPS rate is low; tap it to see which | Needs DB-5 | Haiku · M |
| DB-20 | **One "Download diagnostics" button:** the device log, crash records, settings (no VIN), health and the version in one file | Needs DB-2, DB-3, DB-5 | Haiku · S |
| DB-21 | **A test rig on your bench:** a script plays a recorded GPS file into the board's UART and checks the lap times it prints | Autoflash is off at your request; this would need the port | Opus · M · HW |

## Needs the hardware

No agent can do these. Each one that works moves a line in the README's status table.

| ID | What | Needs |
|---|---|---|
| TR-1 | The DJI Osmo Action remote: pairing, GPS in the video, recording with the sessions, the +8 hour | The camera |
| TR-2 | The RaceChrono DIY profile, and RaceBox Mini mode with RaceChrono, Harry's LapTimer and TrackAddict | A phone with the apps |
| TR-3 | The IMU: found at boot, the mount learned, real g in the log | An LSM6DSO breakout |
| TR-4 | CAN: engine data through the transceiver | An SN65HVD230 and a car |
| TR-5 | The big button | A button and a resistor |
| TR-6 | The battery: a full run from full to the low cutoff, and the sleep current (PW-1, PW-18) | A battery and a meter |
| TR-7 | The OBDb car profiles on the 128i and the Civic | The cars |
| TR-8 | The screen at noon in an open car (the roadmap's first open question) | A sunny day |
| TR-9 | A second OBD adapter (OBDLink CX) and a second GPS module (any cheap 10 Hz NMEA one), to prove the code isn't tied to the two parts on the bench | About $60 |

## An order that works

A suggestion, not a plan. Rows in one step touch different files, so they can run side by side.

| Step | Rows | Why here |
|---|---|---|
| 1 | FX-1, FX-2, FX-8, FX-9, RD-1, RD-3, RD-8, RD-11, DB-1, DB-4 | Small, safe, all Haiku. They make everything after it easier to check |
| 2 | Opus plans HW-1, GP-1 and DS-3 together | The three interfaces have to agree with each other, and with DB-12 |
| 3 | HW-1, then GP-2 to GP-5 and DS-4 | The first pieces of the hardware layer |
| 4 | DB-2, DB-3, DB-5, DB-6, DB-7, DB-9 | Before a second board or a second GPS is tried |
| 5 | PW-1 by hand, then PW-2 to PW-8 | Measured, so each one's gain is known |
| 6 | HW-2 or DS-7: a second board; TR-9: a second GPS | The proof that the layer works |
| 7 | Features, in whatever order you like. FT-1, FT-4, FT-6, FT-7, FT-17, DA-9 and EX-1 are the cheapest parity wins | |

## Sources

Researched on 2026-11-14. Retailer pages disagree in places; the comparison table says where.

- [Garmin: Catalyst 2 announcement](https://www.garmin.com/en-AU/blog/?p=4181) and [Notebookcheck: Catalyst 2](https://www.notebookcheck.net/Garmin-Catalyst-2-head-up-display-with-camera-and-audio-coach-launches.1228621.0.html): 25 Hz, the 3" head-up display, the 1440p camera, spoken coaching, the drag timer, battery life
- [AiM shop: Solo 2 DL](https://www.aimshop.com/products/aim-solo-2-dl-lap-timer-gps-ecu-racing-dash-logger) and [Discovery Parts: Solo 2 DL](https://www.discoveryparts.com/collections/data-video/products/aim-sports-solo-2-dl-data-logging-lap-timer): ECU protocols, predictive references, 10 RGB LEDs, the track database, auto-learning
- [AiM: MyChron 6](https://aim-sportline.com/us/products/mychron6/index.htm) and [Xtra Motorsport: MyChron 6](https://xtramotorsport.com/product/aim-mychron6-karting-data-logger/): RPM from the spark lead, temperature inputs, LEDs, 25 Hz, battery, rolling lap
- [RaceBox](https://racebox.pro) and [RaceBox Mini S at Mode Auto Concepts](https://modeautoconcepts.com/products/racebox-mini-s-automotive-motorcycle-performance-lap-timer-drag-meter-standalone-connection): 25 Hz, stand-alone storage, battery life, the track count
- [Hackster: RaceBox Micro](https://www.hackster.io/news/the-racebox-micro-is-a-teeny-tiny-25hz-bluetooth-gnss-data-logger-for-rc-racing-and-more-cf7c12fe8929): RaceBox protocol and NMEA over Bluetooth
- [HMS Motorsport: Apex Pro Gen II](https://www.hmsmotorsport.com/item/apex-pro-gen-ii): the APEX score, the LEDs, CrewView
- [Racelogic: VBOX Touch lap timing](https://en.racelogic.support/VBOX_Automotive/Product_Info/VBOX_Data_Loggers/VBOX_Touch/VBOX_Touch_(v2%2F%2Fv3)/User_Guide_%E2%80%93_VBOX_Touch_(v2%2F%2Fv3)/05_Display_Modes_%E2%80%93_VBOX_Touch_(v2%2F%2Fv3)/Lap_Timing_Mode_-_VBOX_Touch_(v2%2F%2Fv3)): start and finish modes, delta-T and delta-V
- [Sportsbikeshop: Starlane DaVinci-II R](https://sportsbikeshop.co.uk/motorcycle_parts/content_prod/396104) and [R&G: SpeedAngle](https://rg-racing.com/browsetype/SpeedAngle/Honda): lean angle, live and peak
- [SparkFun: Quectel LG290P](https://www.sparkfun.com/quectel-lg290p-gps-gnss-module-quadband-cut-tape.html): quad-band, 10 Hz by default, 20 Hz maximum
- [u-blox: NEO-M9N product summary](https://content.u-blox.com/sites/default/files/NEO-M9N_ProductSummary_UBX-19027207.pdf): 25 Hz with four systems
- [SparkFun u-blox library: VALSET needs protocol 27](https://github.com/sparkfun/SparkFun_Ublox_Arduino_Library/pull/134.diff) and the [u-blox 8 / M8 protocol specification](https://cdn.sparkfun.com/assets/c/f/d/8/a/u-blox8-M8_ReceiverDescrProtSpec__UBX-13003221__Public.pdf): the older configuration messages
- [Espressif: ESP32-S3 power management](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32s3/api-reference/system/power_management.html): automatic light sleep needs power management and tickless idle built in
- [atomic14: ESP32 display boards compared](https://www.atomic14.com/esp32/boards/displays/) and [Guition JC3248W535](https://www.atomic14.com/esp32/boards/guition-jc3248w535/): the boards worth targeting
