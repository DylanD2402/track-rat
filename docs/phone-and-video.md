# Phone apps and video

Using Track Rat with phone apps (RaceChrono, Harry's LapTimer, TrackAddict), the DJI Osmo Action, and overlay videos. Back to the [README](../README.md).

## Phone apps (Bluetooth)

Track Rat sends its GPS over Bluetooth in the same format as a **RaceBox Mini**, so apps that support that device can use it as their GPS. The board's own screen and lap timing keep working at the same time.

- **Apps with RaceBox Mini support:** RaceChrono and RaceChrono Pro, Solostorm, NMEAconnect, AutoX DL and the RaceBox app. This list comes from the [RaceBox Mini emulator project](https://github.com/anchit92/Open-Source-RaceBox-mini-Emulator) that inspired this feature; I've not tested each app.
- **Connecting:** in the app, add a RaceBox Mini device. The board shows up as `RaceBox Mini` followed by 10 digits (the log prints the exact name at boot). While a phone is connected, a blue **BT** tag shows in the header.
- **Update rate:** one packet per GPS fix, so up to 20 a second (10 in the 10 Hz [GPS mode](user-guide.md#settings)).
- **Accelerometer:** this board has none, so the motion values read as a device lying still. Apps still calculate G-forces from the GPS.
- **Accuracy figures:** the position, speed and heading accuracy fields come straight from the GPS (UBX-NAV-PVT). With a GPS that only sends NMEA they're estimates from the satellite precision value (HDOP).
- **Bench Test:** a bench session streams the simulated car, which is handy for setting up an app at a desk.
- **No pairing:** the Bluetooth link to the phone isn't paired or encrypted, so anyone in range can connect and read your GPS position. Track Rat drops a connection that doesn't start the data stream within 10 seconds, so a stranger can't sit on the connection, but a phone that does subscribe gets the data. Turn Bluetooth off when you don't need it.
- **Turning it off:** set **Bluetooth** to Off in [Settings](user-guide.md#settings). It stops at once and stays off until you turn it on. To leave Bluetooth out of the build altogether, set `BLE_ENABLED` to `0` in `include/Config.h`; the Bluetooth item then disappears from the Wi-Fi page.

Track Rat isn't affiliated with RaceBox. The device name is used only so apps recognise the data format.

### RaceChrono with engine data

**Untested with RaceChrono:** the encodings follow RaceChrono's [DIY Bluetooth API](https://github.com/aollin/racechrono-ble-diy-device) and are tested on a PC, but nobody has connected an iPhone yet.

With **Bluetooth profile** set to **RaceChrono** (then restart Track Rat), it stops being a RaceBox Mini and becomes a RaceChrono "DIY" device named `Track Rat` and 4 digits. It sends its GPS at the fix rate (20 a second) and, with an OBD-II adapter or CAN fitted (see [Bluetooth OBD-II adapter](hardware.md#bluetooth-obd-ii-adapter)), the engine's values as CAN packets. With an IMU fitted, its lateral and longitudinal g go too. Only one profile at a time: iOS has been seen to stop receiving DIY data when a second serial-style service (the RaceBox one) is there.

**In RaceChrono Pro (iPhone or Android):**

1. **Add a DIY device:** in RaceChrono's settings, add an "other device" of the type RaceChrono DIY, Bluetooth LE, and pick `Track Rat ####`. Turn on its GPS and CAN-Bus. (The menu names vary a little between app versions.)
2. **Vehicle profile:** create one for the car (or edit it), then **CAN-Bus channels → Add**, one per value below. *Packet ID* is the decimal of the hex ID; the *Equation* turns the packet into the value.

   | Channel | Packet ID | Equation | Unit |
   |---|---|---|---|
   | Engine RPM | 256 (0x100) | `bytesToInt(raw, 0, 2)` | rpm |
   | Throttle position | 257 (0x101) | `bytesToInt(raw, 0, 2) / 10.0` | % |
   | Coolant temperature | 258 (0x102) | `bytesToInt(raw, 0, 2) / 10.0` | °C |
   | Intake air temperature | 259 (0x103) | `bytesToInt(raw, 0, 2) / 10.0` | °C |
   | Lateral acceleration (IMU) | 272 (0x110) | `bytesToInt(raw, 0, 2) / 1000.0` | G, + = turning right |
   | Longitudinal acceleration (IMU) | 273 (0x111) | `bytesToInt(raw, 0, 2) / 1000.0` | G, + = speeding up |

   RaceChrono then asks Track Rat for just those packets at the rate it wants (until it does, Track Rat sends none). A value Track Rat hasn't had in the last second isn't sent.
3. **Filming with the iPhone:** in RaceChrono, turn on video recording for the session. It films with the phone and draws speed, the map, lap times, g, RPM and throttle on the video, live, from Track Rat's data.
4. **Osmo Action videos:** after the session, in RaceChrono's session view, **Add video** and pick the Osmo Action file; RaceChrono lines it up by its time (check it with the sync slider). With Track Rat as the camera's GPS remote ([DJI Osmo Action](#dji-osmo-action-track-rat-as-its-gps-remote-untested-on-hardware)), the camera's clock and GPS agree with Track Rat's.

The RaceBox profile stays the default: it works with more apps.

#### DJI Osmo Action: Track Rat as its GPS remote (untested on hardware)

**Not yet tried with a camera.** The protocol is ported from DJI's official [Osmo GPS Controller Demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo) (MIT licence) and tested on a PC against the demo's own frames. The DJI R SDK protocol is subject to [DJI's licence terms](http://developer.dji.com/policies/eula/).

The Osmo Action 4 (and 5 Pro, 6, Osmo 360) records GPS only from DJI's GPS remote. Track Rat does that job, with nothing to buy:

- **Pairing, once:** switch the camera on. In **Settings → Camera**, tap **Pair camera**. A 10-second scan lists the DJI cameras it finds; tap yours. Track Rat shows a 4-digit code and the camera asks whether to accept the remote: accept if it shows the same code. After that Track Rat reconnects by itself whenever the camera is on and in reach (every 5 to 10 seconds), without asking again. **Forget** disconnects and forgets it. Rejected on the camera, Track Rat stops asking until you pair again.
- **GPS into the video:** 10 times a second, position, height, north/east/down speed, accuracies and satellites. The camera writes them into the video, and DJI Mimo and DJI Studio then draw speed, route and altitude dashboards with no other software.
- **Recording follows the session** (with **Camera recording** on *With sessions*): it starts when you drive off in a session and stops when you park in the pits (the debrief), and starts again when you go back out. If the camera doesn't follow, Track Rat asks twice more, then leaves it. Starting or stopping on the camera itself is left alone until the next time you go out or park. *Manual* never starts or stops it.
- **In the header:** **CAM 78%** (blue) while the camera is really there and not recording, **REC 78%** blinking slowly red while it records, with the camera's battery, and an orange **CAM HOT** when the camera says it's too hot to record. "There" means the handshake is done *and* the camera's status (about twice a second) came in the last 3 seconds (`CAMERA_ALIVE_MS`): a paired camera that is off or out of reach shows nothing, and one switched off drops the tag within 3 seconds, without waiting for Bluetooth to notice. There is no bare **CAM** before the first status. Settings → Camera shows *Linked: no status from the camera* meanwhile.
- **Tap the tag to record:** tapping **CAM** starts recording, tapping **REC** stops it, on any screen whose top bar shows it (the menu, waiting at the track, driving, Free Roam, Performance). It reads **REC...** or **STOP...** until the camera's status follows (about half a second). It works with either Camera recording setting. With *With sessions*, a tap is treated like the camera's own button: the sessions leave it alone until the next time you drive off or park.
- **For overlays (6.4):** the GPS time the camera started recording is saved in the session's summary as `cameraStartUtc`, from the camera's status (it reports twice a second, so to about half a second).
- **Caveats:** the camera has one remote slot, so DJI Mimo can't be its remote at the same time. DJI's demo sends the time 8 hours ahead (China time) with the date unchanged, and Track Rat does the same; if the video's GPS times come out 8 hours ahead, set `DJI_HOUR_OFFSET 0` in `Config.h`. `CAMERA_BLE_ENABLED 0` leaves it out. It shares the Bluetooth radio with the phone and the OBD adapter: only one of the two connects or scans at a time.

## Overlay videos

Track Rat's log drawn over your video: the track map with your dot, speed, lap, lap time and the delta to your best lap, the sectors (purple your best, green better than last lap, yellow slower), a g circle (the IMU's g if one is fitted, else from the GPS), and RPM, throttle, coolant and oil temperature when the log has engine data (temperatures in °F with `--units mph`). `tools/overlay.py` makes it on your PC, free. Or use a tool you already have: the Wi-Fi page's **CSV** button (below), or RaceRender, which reads the `.vbo` itself.

**Set-up, once:** Python 3 and

```bash
pip install -r tools/requirements-overlay.txt
```

That's Pillow and imageio-ffmpeg, which brings its own ffmpeg. Copy the session's `.vbo` and `.json` (same name) off the SD card, or download both from the Wi-Fi page, into one folder.

**With an Osmo Action 4 paired to Track Rat** ([DJI Osmo Action](#dji-osmo-action-track-rat-as-its-gps-remote-untested-on-hardware)): the camera started recording when you drove off, and Track Rat noted the GPS time it did (`cameraStartUtc` in the summary). So:

```bash
python tools/overlay.py 20261020_140300_calabogie.vbo --burn DJI_20261020140301_0001_D.MP4 --sync camera
```

`--camera-start 1` for the second video of the session (the second time out of the pits), and so on.

**With an iPhone (or any camera that knows the time):** the video's own creation time lines it up to about a second. Look at the result and correct it with `--nudge`, in seconds (+ moves the data later):

```bash
python tools/overlay.py 20261020_140300_calabogie.vbo --burn IMG_1234.MOV --sync creation --nudge -0.4 --units mph
```

**Any camera, exactly:** film Track Rat's screen for a moment while you tap the **top bar** of the summary or the debrief. It shows **SYNC** and the GPS time, white, for 3 seconds, and notes the time in the summary (`syncFlashesUtc`). Find the frame where SYNC first appears (any video player shows frame numbers, or step through in an editor) and give it:

```bash
python tools/overlay.py 20261020_140300_calabogie.vbo --burn GOPR0042.MP4 --sync-frame 412
```

`--sync-index 1` for the second flash. Or give the time of the first frame yourself: `--start 2026-10-20T14:03:12.3Z`.

**Output:** `--burn VIDEO` writes the video with the overlay drawn on (`..._overlay.mp4`, the sound kept). Without it you get a transparent overlay to put on a track above the footage in an editor (DaVinci Resolve, Final Cut, CapCut): ProRes 4444 `.mov` (the default; large) or, with `-o name.webm`, VP9 with transparency (small, slower to make). Then `--video VIDEO` sets its length, size and frame rate from the footage, or `--duration SECONDS` and `--size 1920x1080`. 30 frames a second by default (`--fps`); burning uses the video's own rate. It prints how fast it's going and how long is left. Frames that show the same as the last aren't drawn again, so a parked car renders quickly.

**CSV for other tools:** on the Wi-Fi page's **Sessions** tab, **CSV** next to a session with a log makes a `.csv` on the phone from its `.vbo`, for Telemetry Overlay, RaceRender and DashWare: `utc`, `lat`, `lon`, `speed_kmh`, `lap`, `lap_time_s`, `delta_s` (to the session's best lap at the same point on track), `g_lat`, `g_long`, `rpm`, `throttle_pct`, `coolant_c`, `oil_c` (empty when not logged, or not known at that moment).
