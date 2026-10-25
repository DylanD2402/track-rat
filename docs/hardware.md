# Hardware

The parts, how to wire them, what the GPS sends, and the optional add-ons. Back to the [README](../README.md).

![Wiring](images/wiring.svg)

## Hardware

| Part | Notes |
|---|---|
| ES3C28P board | ESP32-S3 N16R8, ILI9341V 240x320 IPS, FT6336G capacitive touch, SDIO microSD |
| Beitian BE-252Q | u-blox M10 over UART, factory default 115200 baud (auto-detected) |
| microSD card | Optional, FAT32. Holds `tracks.json` and session logs |

### Wiring

Plug the BE-252Q into the board's **UART** connector (GPIO 43/44): GPS TX and RX to the connector's TX/RX, plus 5V and GND. The expansion header (GPIO 14/21) also works.

At boot the firmware tries both places and both orientations, so it doesn't matter if TX and RX are crossed. The GPS screen shows which GPIO it found the GPS on.

The BE-252Q's compass wires (SDA/SCL) aren't used.

### GPS data

The BE-252Q is built on a u-blox M10. At boot Track Rat asks it for 20 fixes a second (10 if the [GPS mode setting](user-guide.md#settings) is on all systems) as binary **UBX-NAV-PVT** messages, and once those arrive it turns the NMEA text sentences off. Each message carries the time to the millisecond, position, speed, vertical speed, and the GPS's own estimate of how accurate the position and speed are.

- **Other GPS modules:** if no UBX-NAV-PVT arrives within 3 seconds, Track Rat reads NMEA text instead (GGA and RMC) as before. There are no accuracy figures then, only HDOP.
- **Settings stay in the GPS's memory, never its flash:** the rate and messages in RAM, the satellite systems and AssistNow Autonomous also in its battery-backed RAM. If the GPS loses power and comes back sending NMEA, Track Rat asks for UBX-NAV-PVT again.
- **Standby while the board sleeps:** whenever the board sleeps or is [switched off](user-guide.md#battery-power-off-and-sleep), the GPS goes into u-blox software standby (UBX-RXM-PMREQ). It draws a few tens of microamps instead of about 20 mA, and keeps its clock, satellite orbits and last position, so it has a fix about a second after waking if it slept under 4 hours (how long broadcast orbits last). At start-up the board wakes it with a short pulse on its RX line.
- **AssistNow Autonomous:** turned on at boot (it's off on a new M10). From the orbits it downloads, the GPS works out the satellites' orbits 3 to 6 days ahead, so after a day or two off it still finds its position in seconds rather than half a minute. The first fixes are a little less accurate until fresh orbits arrive, about half a minute later.
- **Unplugging the battery** loses all of that once the module's small backup capacitor runs down, and the next start is a cold start: about 30 s with open sky, longer in a car. Switch off with [Power off](user-guide.md#battery-power-off-and-sleep) instead.
- **GPS Status screen:** the Data line says `UBX` or `NMEA`. With a u-blox GPS the Fix line shows position and speed accuracy, for example `LOCKED   ±1.2 m   ±0.5 km/h   3D`.

Everything else (display, touch, SD) is built into the board. The pins are set in `include/Config.h` and `platformio.ini`.

An optional big button and LED delta bar can go on IO21 and IO2: see [Big button and LED bar](#big-button-and-led-bar).

## Big button and LED bar

Two optional add-ons on the expansion header. Both are off the screen, so they work with gloves and in sun. Neither is needed; the timer works the same without them.

### Parts

| Part | Notes |
|---|---|
| Push button | Momentary, normally open. A 25-30 mm panel-mount arcade button is good with gloves |
| WS2812B LED strip | 10 LEDs by default (8-12 works). Any 5 V "NeoPixel" type strip |
| 330-470 ohm resistor | In series with the strip's data line |
| 1 kohm resistor | In series with the button |
| 100 nF capacitor | Across the button (optional) |
| 470-1000 uF capacitor | Across the strip's 5 V and GND, close to the strip |
| 74AHCT125 or 74HCT245 | Optional 3.3 V to 5 V level shifter for the strip's data line |

### Wiring

| Add-on | Connect | Pin |
|---|---|---|
| Button | one side to IO21, the other to GND | IO21 |
| LED bar data | strip DIN to IO2 | IO2 |
| LED bar power | strip 5 V to a 5 V supply, strip GND to the board's GND | |

- **Button:** no external pull-up is needed; the board's internal one is used. Put the 1 kohm resistor in series with the button. IO21 is also a GPS pin, and the board briefly drives it at start-up while it looks for the GPS, so the resistor stops a button held down at that moment from shorting the pin. The 100 nF across the button smooths contact bounce, but isn't required, as the firmware debounces too.
- **LED bar power:** the strip needs 5 V, not 3.3 V. Each LED draws up to 60 mA at full white. The default brightness (`LED_BAR_BRIGHTNESS` 64 of 255) keeps 10 LEDs under about 200 mA. Power the strip from a 5 V supply that isn't the board's 3.3 V pin, and join its GND to the board's GND. Check your board's header pinout for a 5 V pin before using it for the strip.
- **LED bar data:** the board's 3.3 V output is a little under what a WS2812B strictly wants (0.7 x 5 V = 3.5 V), but it normally works on a short wire. If the strip flickers or shows random colours, use the level shifter. Keep the data wire short, and put the 330-470 ohm resistor right at the strip's DIN end.
- **Strip capacitor:** the 470-1000 uF capacitor stops the switch-on surge from upsetting the strip. Connect it before powering up, and always connect GND before 5 V.
- **Mounting:** DIN is the end of the strip with the arrow pointing away from it. Mount it so the first LED is at the left (from the driver's seat). If "ahead" grows to the left on your strip, set `LED_BAR_REVERSED` to `1`.
- **Board LED:** the board's own RGB LED (IO42) needs no wiring.

### Button

The button does what BOOT does: a short press is "next / switch view" and a hold of 1 s is "select / start / end the session". See the BOOT column in the table under "Using it". Unlike BOOT, its hold acts as soon as it reaches 1 s, while still held, and it never switches the unit off.

### LED bar

The bar lights up while a lap is running and the best lap's delta is known. The middle of the strip is zero:

- **Ahead of your best lap:** green LEDs grow to the right from the middle.
- **Behind your best lap:** red LEDs grow to the left from the middle.
- **How many:** the whole side is lit at 1 s ahead or behind (`LED_BAR_FULL_SCALE_MS`), with at least one LED once you're more than 0.02 s out. A bigger gap clamps at a full side.
- **On the pace** (within 0.02 s): a dim green marker in the middle.
- **New best lap:** the whole bar flashes purple for about 2 s.
- **Each split:** a short white blink (0.2 s).
- **Timing but no delta yet** (before your first lap is done, or off the best lap's line): a dim amber marker in the middle.
- **Not timing** (menus, summaries, no GPS fix): off.

The board's own LED shows the same colour (green ahead, red behind, brighter for a bigger gap, purple and white for the same events), so you can test the logic without any strip. It's easiest to see with the bench test.

The LEDs are updated 40 times a second, twice the GPS rate. The strip is driven by the ESP32-S3's RMT peripheral, which sends the colours by itself, so the main loop doesn't wait for it. With no strip fitted, nothing happens and nothing breaks.

### If the GPS is on IO14/IO21

The GPS can be plugged into the expansion header instead of the UART connector (see Wiring above). It then uses IO21, so the button can't be used, and the log says so:

```
Input: big button disabled - the GPS is on IO21
```

The same check covers every add-on pin: any add-on on a pin the GPS ended up on stays off. The LED bar on IO2 and the board LED are unaffected by a GPS on IO14/IO21.

### Add-on options

These are set in `include/Config.h`.

| Option | Default | What it does |
|---|---|---|
| `EXT_BUTTON_ENABLED`, `EXT_BUTTON_PIN` | 1, 21 | The big button |
| `LED_BAR_ENABLED`, `LED_BAR_PIN` | 1, 2 | The LED strip |
| `LED_BAR_COUNT` | 10 | LEDs on the strip (2 to 64) |
| `LED_BAR_BRIGHTNESS` | 64 | 0-255. Turn it up in sun, down for less current |
| `LED_BAR_REVERSED` | 0 | 1 = flip left and right |
| `LED_BAR_FULL_SCALE_MS` | 1000 | Delta that lights a whole side of the bar |
| `BOARD_LED_ENABLED`, `BOARD_LED_PIN`, `BOARD_LED_BRIGHTNESS` | 1, 42, 40 | The board's own LED |
| `IMU_ENABLED` | 1 | Look for an IMU on the I2C header at boot (see below). Harmless with none fitted |
| `CAN_ENABLED`, `CAN_TX_PIN`, `CAN_RX_PIN` | 0, 3, 14 | OBD-II over CAN (see below). Off by default |
| `CAN_BITRATE_KBPS` | 500 | The car's CAN speed: 500 for most cars since 2008, some use 250 |
| `SHIFT_LIGHT_START_RPM`, `SHIFT_LIGHT_RPM` | 5500, 6800 | Where the shift light starts filling, and where it flashes to shift. 0 = no shift light |

### CAN / OBD-II (optional, untested on hardware)

**Not yet tried on a board or a car: no transceiver has been fitted.** The OBD-II requests and replies, the polling and the shift light are tested on a PC; the CAN driver is the ESP32-S3's own (TWAI).

An SN65HVD230 CAN transceiver board (3.3 V) on IO3 (its TX/D pin) and IO14 (its RX/R pin), 3.3 V and GND, wired to the car's OBD-II socket: CAN high to pin 6, CAN low to pin 14, ground to pin 5. Set `CAN_ENABLED 1` in `Config.h` and rebuild. It's **off by default** because IO14 is also one of the GPS pins on the expansion header: with the GPS found there, CAN stays off. The serial log says when the engine answers.

- **What it reads:** engine speed, throttle position and coolant temperature, the standard OBD-II values every petrol car since about 2008 gives. Engine speed is asked for about 12 times a second, the other two about 6.
- **In the log:** every `.vbo` gets five more channels, **RPM**, **Throttle position**, **Coolant temperature**, **Intake temperature** and **Oil temperature** (0 when the engine hasn't said in the last second, 3 seconds for the temperatures; CAN doesn't ask for the intake, so it stays 0, and so does oil on cars without it). Circuit Tools shows them like any other channel. Logs from before oil temperature have the first four.
- **Oil temperature:** asked for once a second as well, for cars that report it (PID 0x5C). A car that doesn't answer it three times running isn't asked again.
- **Coolant in the top bar:** the coolant temperature, coloured by Settings → Coolant, and a full-screen warning when it's too hot: see [Engine temperatures](#engine-temperatures-coolant-and-oil).
- **Shift light:** from `SHIFT_LIGHT_START_RPM` the LED bar fills left to right, green, then yellow, then red; at `SHIFT_LIGHT_RPM` the whole bar flashes blue. Below the start it shows the delta as usual. The board's own LED keeps the delta. Set both for your engine.
- It only asks: it never writes to the car or clears codes. With the ignition off nothing answers, and Track Rat carries on without it.

### Bluetooth OBD-II adapter

**Tried with a Veepeak OBDCheck BLE+** (October 2026): engine speed, throttle, coolant and intake temperatures logged on the road and on screen. Other adapters haven't been tried. The ELM327 conversation is also tested on a PC against recorded adapter replies.

The same engine data as CAN, with nothing to wire: a **Bluetooth LE** OBD-II adapter plugged into the car's OBD-II socket. It must be a Bluetooth LE model, as the ESP32-S3 has no Classic Bluetooth: the Veepeak OBDCheck BLE+ (about $30) or the OBDLink CX (faster, CAN cars only). The Veepeak Mini, Wi-Fi adapters and the OBDLink MX+ can't connect.

- **Choosing it:** plug the adapter in and switch the ignition on. In **Settings → Engine data**, tap **Find adapter**. A 10-second scan lists what it finds by name and signal strength (names with OBD, OBDII, VEEPEAK, OBDLink, vLink or IOS-Vlink, and anything offering the FFF0 or FFE0 service). Tap yours. Track Rat remembers it and connects by itself whenever it's in reach, trying every 5 to 10 seconds. **Forget** disconnects and forgets it. With no adapter chosen, nothing happens at all.
- **In the header:** a blue **OBD** tag only while the car's engine is answering through the adapter, beside **BT**, until the engine sends its coolant temperature; then the temperature takes its place ([Engine temperatures](#engine-temperatures-coolant-and-oil)). Nothing shows while the adapter is still setting up or finding the car (it answers SEARCHING...), with the ignition off (it answers NO DATA), or when a remembered adapter is out of reach, unplugged or still connecting.
- **"Connected" in Settings** means the adapter itself is there: the Bluetooth link is up *and* it has sent something in the last 6 seconds (`OBD_ALIVE_MS`), whether or not the car answers. The Engine data screen says *Linked: waiting for it to answer* while the link is up and it hasn't yet, then what it's doing (setting up, finding the car, no answer from the car, reading the engine).
- **What it reads:** engine speed, throttle, coolant, intake air and oil temperature, whichever the car supports (it asks the car first). Oil temperature (PID 0x5C) is only on some cars. Cars that only report the pedal give that as the throttle. Engine speed is asked for most, then throttle; the temperatures once a second. Cheap adapters manage 6–15 requests a second; with the two speed-ups below an ELM327 reaches 20–40 on a CAN car, so engine speed about 10 Hz and throttle about 7 at 20 a second. Each value is stamped at the middle of its request.
- **Speed-ups, tried once per connection:** the "one reply" digit (`010C1`: the adapter answers as soon as the engine has, instead of waiting out its timeout) and engine speed and throttle in one request (`010C11`). Clones that answer `?` simply go without.
- **The Engine data screen** shows the adapter, what it's doing (setting up, finding the car, reading the engine, or no answer from the car with the ignition off), the car's protocol, requests a second, and each channel's rate.
- **Same as CAN from there:** the `.vbo` gets the five engine channels from the moment an adapter has been chosen, and the shift light and the coolant temperature work the same. With both fitted, CAN wins.
- **Alongside the phone:** the RaceBox output keeps advertising and working while the adapter is connected. Up to four Bluetooth connections at once (the phone, the adapter, and room for the camera).
- Like CAN, it only asks: it never writes to the car or clears codes. `OBD_BLE_ENABLED 0` in `Config.h` leaves it out.

### Car-specific data: brake pressure, steering angle (OBDb car profiles)

**Untested in a car.** The requests, the replies and the adapter's settings are tested on a PC, and the decoding against 6,637 replies OBDb recorded from real BMW 1 Series and Honda Civic cars. Whether *your* car answers is the open question: see the two cars below.

The standard OBD-II requests only cover the engine. Most cars' other modules answer their maker's own requests too: brake pressure and steering angle from the stability control, wheel speeds, gear. **[OBDb](https://github.com/OBDb)** documents these per make and model. With a **car profile** on the SD card, Track Rat asks for them through the Bluetooth adapter alongside the engine's values and logs them.

**Making a profile** (on your PC, Python 3, no extra packages):

```bash
python tools/obd_car.py list BMW-1-Series --year 2010 --find brake
python tools/obd_car.py make BMW-1-Series --year 2010 --signals 1SERIES_BRAKE_P,BMW_STEERING_WHEEL_ANGLE,BMW_YAW_RATE --name "BMW 128i" --vin WBAUP --hz 10
```

- **`list`** downloads the model's and the make's signal sets from OBDb (`BMW-1-Series` and `BMW`), keeps what's for that model year, and lists each signal with its ID, name, module and request. The last column says what OBDb knows for that year: **seen** (recorded answering on a car of that year), **not seen** (recorded *not* answering), **unverified** (not checked for that year), or nothing.
- **`make`** writes `obd/<name>.json` with only the signals you chose, up to 8. **`--hz`** sets how often they're asked (OBDb's own rate otherwise, often 4 a second). **`--vin`** is the start of your car's VIN (from the windscreen or the door pillar): with several profiles on the card, the one whose VIN start matches the car is used. A profile with no VIN is used for any car, as long as it's the only one without.
- Copy the `obd` folder to the root of the SD card. Profiles are read at start-up.

**On the car:** with profiles on the card, Track Rat reads the car's VIN once connected, and picks the profile. Then it takes turns: four standard requests, then up to six of the car's, with the adapter pointed at the car's module (headers on, its CAN IDs, BMW's extended address) and back. Only the settings that change are sent. On a Veepeak-class adapter that's about 10 brake-pressure readings a second, with engine speed and throttle still about 6 a second. A request the module doesn't answer, or refuses, is asked again only every 10 s.

- **The Engine data screen** shows *Car: BMW 128i* and each signal's latest value, or why there's none: *no reply*, *not supported*, or *adapter can't ask* (an old clone without extended addressing, `ATCEA`, can't reach a BMW module). Or *Car: no profile on the card for its VIN*.
- **In the log:** each signal is a channel of the `.vbo`, after the engine's, named and in units as OBDb has them (*Brake pressure*, bar), so Circuit Tools graphs it like any other. The header says which profile it came from. A value not read in the last three of its requests is 0. A car recognised after the session's log has started is in the next session's log.
- **The Wi-Fi page's CSV** gets them too, as extra columns (`brake_pressure`, `steering_wheel_angle`).
- **The serial log** gives only the first 8 characters of the VIN (make, model and body), not the part that identifies the car.

**Your two cars,** from what OBDb has recorded:

| Car | Brake pressure | Steering angle |
|---|---|---|
| BMW 128i (E82, 2010) | `1SERIES_BRAKE_P` (`22 DBE5` to the stability control, `6F1`/`629`, address 29). **Unverified for 2010**, and a 2010 1 Series in OBDb didn't answer its neighbour `22 DBE4` (wheel speeds), so it's likely written for newer BMWs | `BMW_STEERING_WHEEL_ANGLE` and `BMW_YAW_RATE` (`21 04`, the older E-series style): unverified for 2010, but worth trying |
| Honda Civic Si (2008) | `HONDA_BRK_P` (`22 4005`) and `HONDA_BP` (`22 4A7C`) from the VSA module (29-bit IDs, `18DA28F1`): unverified for 2008, from Honda's make-wide set | `HONDA_STEER_ANGLE` (`22 4C2D`): unverified |

A minute of driving settles it: the Engine data screen shows which answer. What's learned is worth sending back to OBDb, which records exactly this.

**Licence:** OBDb's data is [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/). Track Rat's code (MIT) has no OBDb data in it: the firmware only reads profiles from the card, and `tools/obd_car.py` downloads OBDb's files when you run it. A profile is an adaptation of OBDb's data, so it's CC BY-SA 4.0 too: the tool writes the licence, OBDb's repositories and what was changed into the file. Share one under the same licence. See [DATA_LICENSE.md](../DATA_LICENSE.md).

### Engine temperatures (coolant and oil)

**Untested on hardware,** like the engine data itself. Oil temperature also depends on the car reporting it (standard PID 0x5C): many don't.

With engine data (CAN or a Bluetooth adapter), Track Rat keeps an eye on the engine's temperatures for you:

- **In the top bar:** the coolant temperature, such as **92°**, in place of the blue **OBD** tag, as soon as the engine has sent one. It's **blue** while the engine is cold (below *Cold below*, 70 °C), plain grey when warm, **yellow** from *Warm at* (100 °C) and **red, blinking slowly** from *Hot at* (110 °C). If the engine stops sending it for 3 seconds, the tag goes back to **OBD**. In °F with Speed units in mph.
- **The overheat warning:** when the coolant reaches *Hot at*, or the oil reaches *Oil hot at* (130 °C), the whole screen flashes red and yellow for 10 seconds with **COOLANT TOO HOT**, the temperature (e.g. **112°**) in big figures and *Ease off and let it cool* (**OIL TOO HOT** for oil), on any screen, and the LED bar flashes red and yellow with it. A tap or a press closes it early. Then the screen comes back with the blinking red tag. It shows again only once the temperature has dropped 3 °C below the limit and come back up, or a minute later if it's still too hot. The limits are in **Settings → Coolant** and on the Wi-Fi page.
- **After the session:** the summary and the pit debrief show the hottest the coolant got, **Coolant max 104°** (and the oil's), in the same colours. The debrief's laps page has each lap's hottest coolant beside its time. The summary file has them as `coolantMaxC` (and `oilMaxC`), for the session and in each lap, in °C; only when the engine sent them. A session recovered from its log, or re-timed, gets them again from the log's temperature channels.

`COOLANT_COLD_C`, `COOLANT_WARM_C`, `COOLANT_WARN_C` and `OIL_HOT_C` in `Config.h` are only a new board's defaults; `TEMP_ALERT_MS`, `TEMP_REARM_DROP_C` and `TEMP_REPEAT_MS` set the warning's 10 s, 3 °C and one minute.

### IMU (optional, untested on hardware)

**Not yet tried on a board: no IMU has been fitted.** The maths is tested on a PC; the I2C driver is written to the datasheet.

An LSM6DSO-class IMU (LSM6DSO, DSOX, DSR, DSL, DS3, DS3TR-C: a breakout from Adafruit, SparkFun or Pololu) on the board's I2C header, alongside the touch controller: 3.3 V, GND, SDA (IO16) and SCL (IO15). It's found at boot at address 0x6A or 0x6B (the touch controller is at 0x38); the serial log says which. With none fitted, nothing changes.

- **Mounted any way round:** it learns how at every boot. **Up** from two seconds of standing still, **forward** from speeding up and braking in a straight line (about 8 g of it added up: a few seconds of driving). Until then the g-forces still come from the GPS.
- **Then** the `.vbo` log's lateral and longitudinal g are the IMU's (+ = turning right, + = speeding up, as before), and the RaceBox Bluetooth packet carries its g (x forward, y right, z up, in milli-g) and rotation rates (roll, pitch, yaw) instead of a device at rest. Real g is quicker and smoother than g from the GPS, which lags by a fix or two.
- Read at 104 Hz and averaged between GPS fixes; ±4 g and ±500 °/s.
