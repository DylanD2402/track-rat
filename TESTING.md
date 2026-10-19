# First flash and test

Work through these in order. Each step checks one part of the hardware, so if something's wrong you'll know which part. Every step logs what it's doing over USB. Read the log with `python tools/serial_log.py --port COM4 --seconds 20 --reset` (use your own port).

## 0. Before you start

- Install PlatformIO. The first build downloads the ESP32 toolchain, which takes a few minutes.
- Connect the board by USB-C.
- The board resets itself for uploads. Only if an upload fails with "Failed to connect" or "No serial data received": hold **BOOT**, tap **RST**, release **BOOT**, then upload again.

## 1. Display and touch: bench test (no GPS needed)

```bash
pio run -e es3c28p -t upload && python tools/serial_log.py --port COM4 --seconds 30
```

Expected:

1. The boot screen says **TRACK RAT** in white on **black**.
   - **Background is white or colours look wrong:** set `DISPLAY_INVERT` to `0` in `include/Config.h` and re-flash.
   - **Image is upside down:** set `DISPLAY_ROTATION` to `3`.
2. The serial log shows `Display: 320x240, sprite buffer in PSRAM`.
   - If it says `NO sprite`, PSRAM isn't enabled. Check `board_build.arduino.memory_type = qio_opi` in `platformio.ini`.
3. The serial log shows `Input: touch controller found`.
4. With no GPS fix, the "waiting for GPS" screen stays up. With a fix, the track list opens.
5. Tap the screen to open the track list. The serial log prints `Touch: raw=(..) screen=(..)`.
   - **Wrong button triggered:** adjust `TOUCH_SWAP_XY` / `TOUCH_INVERT_X` / `TOUCH_INVERT_Y` until a tap at the left edge gives a small screen x and a tap at the top gives a small screen y.
6. Tap **>** and **<** to browse tracks. Go to **Bench Test** (the last one), tap **SELECT**, then **START**. A yellow **BENCH** tag appears in the header.
7. The simulated car is already driving, so the out lap ends the next time it passes the start/finish, within about 15 s. Lap 1 starts then. Laps take about 15 s.
   - At the half-way split, "S1" and a split time appear. From lap 2 on, a +/- delta also appears.
   - At each lap end, the lap time shows full-screen. It's green when it's a new best.
   - The serial log shows `LAP n: m:ss.mmm` and `Saved SD:/sessions/...`.
8. Hold the screen for 2 s. A progress bar fills, then the **summary** appears and lists your laps.
9. If you have an SD card in, look for `/sessions/SIM_..._bench.json` and `SIM_..._bench.vbo` on it. The `.vbo` opens in Racelogic's Circuit Tools (see "Session logs" in the README).

The **BOOT** button also works on every screen: press = next/back, hold 1 s = select/start/end.

## 2. GPS link: real build, indoors is fine

Plug the BE-252Q into the board's UART connector (or GPIO 14/21 on the expansion header), then:

```bash
pio run -e es3c28p -t upload && python tools/serial_log.py --port COM4 --seconds 30
```

Expected in the serial log:

```
GPS: detected NMEA at 115200 baud (GPS TX -> GPIO 44, GPS RX -> GPIO 43)
GPS: requested 10 Hz (u-blox VALSET, RAM only)
GPS: data=yes sats=0 hdop=99.9 fix=no rate=0.0Hz
```

The "waiting for GPS" screen should show **NMEA OK @ 115200, GPIO nn** in green.

- **`no NMEA detected` / NO DATA:**
  - Make sure the GPS was plugged in **before** the board started. Press RST to re-detect.
  - Check that the module's LED is on, and that the plug is fully seated.
- **To see the raw sentences:** set `GPS_ECHO_NMEA` to `1`.

## 3. GPS fix: outdoors

Take it outside with a clear view of the sky. The first fix after power-up can take 30 s to a few minutes.

- **Sats** climbs and **Fix** turns green, LOCKED.
- **Rate** shows the update rate.
  - **10 Hz:** the module accepted the u-blox configuration.
  - **1 Hz:** the chip ignored it, and timing still works.
  - Either way, please report what you see. The BE-252Q's chipset isn't documented, and this tells us what it is.
- If you're more than 5 km from every track, the track list opens. Otherwise the nearest track's Ready screen opens.

## 4. Real lap test (car park or track)

To test without a race track, add a temporary track whose start/finish is a spot you can drive or walk past. Get the coordinates from the GPS screen while standing on the spot, or from Google Maps. Put it in `tracks.json` on the SD card:

```json
{ "tracks": [ { "id": "test", "name": "Car Park Test", "location": "Here", "country": "",
  "length": 300, "startFinish": { "lat": 00.000000, "lon": 00.000000, "radius": 10 } } ] }
```

Laps shorter than 20 s are ignored (`MIN_LAP_TIME_MS`). Lower it for walking-pace tests.

Then select the track and press START. Pass the point to start lap 1 and pass it again to finish each lap. Moving under 5 km/h doesn't count, so walk briskly or drive.

## 5. Battery, sleep and SD update (optional)

Needs a 3.7 V LiPo in the battery socket for the battery steps.

1. **Battery gauge.** With the battery fitted, the serial log prints `Battery: pin ... mV, battery ... mV` every 10 s, and a percentage shows in the top bar. Compare the log's battery voltage with a multimeter on the battery; if it's off, set `BATTERY_CAL_OFFSET_MV`.
2. **No battery.** Unplug the battery (USB connected). The log should say `Battery: none fitted` and the top bar shows no percentage. If it still shows one, send the `pin ... mV` figure from the log.
3. **Auto-sleep.** To avoid waiting 20 minutes, set `AUTO_SLEEP_IDLE_MS` to `30000` and `AUTO_SLEEP_DEEP_AFTER_MS` to `10000`, flash, and leave the board alone. The backlight goes off after 30 s and the log says `deep sleep` 10 s later. Touch the screen: the board restarts. Sleep it again and press BOOT: same. Set the two values back afterwards.
4. **Low battery.** With a partly charged battery, set `BATTERY_LOW_MV` just above the logged battery voltage and flash. After 10 s the screen should say **BATTERY LOW** and then go to sleep. Set it back to `3300`.
5. **SD update.** Build, copy `.pio/build/es3c28p/firmware.bin` to the SD card root as `trackrat.bin`, put the card in and reset. Tap **INSTALL**. After the progress bar the board restarts, and the card now has `trackrat.installed.bin` instead. Also try a wrong file (rename any text file to `trackrat.bin`): it should be refused.

## What to report back

If something doesn't work, these four things are enough to diagnose it:

- the serial log from boot
- a photo of the screen
- which step failed
- the GPS **Rate** value from step 3
