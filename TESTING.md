# First flash and test

Work through these in order. Each step checks one part of the hardware, so if something's wrong you'll know which part. Keep the serial monitor open (`pio device monitor`) the whole time. Every step logs what it's doing there.

## 0. Before you start

- Install PlatformIO. The first build downloads the ESP32 toolchain, which takes a few minutes.
- Connect the board by USB-C.
- If upload fails with "No serial data received", hold **BOOT**, tap **RESET**, release **BOOT**, then upload again.

## 1. Display and touch: simulator build (no GPS needed)

```bash
pio run -e es3c28p-sim -t upload && pio device monitor
```

Expected:

1. The boot screen says **TRACK RAT** in cyan on **black**, with "SIMULATOR BUILD".
   - **Background is white or colours look wrong:** set `DISPLAY_INVERT` to `0` in `include/Config.h` and re-flash.
   - **Image is upside down:** set `DISPLAY_ROTATION` to `3`.
2. The serial log shows `Display: 320x240, sprite buffer in PSRAM`.
   - If it says `NO sprite`, PSRAM isn't enabled. Check `board_build.arduino.memory_type = qio_opi` in `platformio.ini`.
3. The serial log shows `Input: touch controller found`.
4. The simulator "finds" its track and opens the **Ready** screen for "Simulator Loop".
5. Tap the **left** third of the screen. You should land on the track list, and the serial log prints `Touch: raw=(..) screen=(..)`.
   - **Wrong button triggered:** adjust `TOUCH_SWAP_XY` / `TOUCH_INVERT_X` / `TOUCH_INVERT_Y` until a tap at the left edge gives a small screen x and a tap at the top gives a small screen y.
6. Tap **>** and **<** to browse tracks, then tap **SELECT** on Simulator Loop, then **START**.
7. The simulated car is already driving, so the out lap ends the next time it passes the start/finish, within about 40 s. Lap 1 starts then. Laps take about 35–40 s.
   - At the half-way split, "S1" and a split time appear. From lap 2 on, a +/- delta also appears.
   - At each lap end, the lap time shows full-screen. It's green when it's a new best.
   - The serial log shows `LAP n: m:ss.mmm` and `Saved SD:/sessions/...`.
8. Hold the screen for 2 s. A progress bar fills, then the **summary** appears and lists your laps.
9. If you have an SD card in, look for `/sessions/SIM_..._simulator.json` on it.

The **BOOT** button also works on every screen: press = next/back, hold 1 s = select/start/end.

## 2. GPS link: real build, indoors is fine

Plug the BE-252Q into the board's UART connector (or GPIO 14/21 on the expansion header), then:

```bash
pio run -e es3c28p -t upload && pio device monitor
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

## What to report back

If something doesn't work, these four things are enough to diagnose it:

- the serial log from boot
- a photo of the screen
- which step failed
- the GPS **Rate** value from step 3
