# Flashing Track Rat

Four ways to put the firmware on the board, from easiest to most hands-on.

## Install from your browser

Go to **https://dyland2402.github.io/track-rat/** and press **Install**.

- **You need:** Chrome or Edge on a computer (not a phone, not Firefox or Safari), and a USB-C cable that carries data.
- **Download mode:** if the port list is empty, hold **BOOT**, tap **RST**, then let go of **BOOT**, and try again.
- **Erase warning:** installing erases the board's internal flash. Sessions and tracks saved there (a board with no SD card) are lost. The SD card is not touched. Choosing "erase" in the dialog also resets your settings.
- When it finishes, press **RST** if the screen stays dark.

## Download a release and use esptool

1. Open the [releases page](https://github.com/DylanD2402/track-rat/releases) and download `trackrat-<version>-full.bin` (and `SHA256SUMS` if you want to check it).
2. Install esptool: `pip install esptool`.
3. Write the full image at offset 0x0. The image is for the ESP32-S3 with 16 MB of flash.

esptool v5:

```bash
esptool --chip esp32s3 --port COM4 --baud 115200 write-flash 0x0 trackrat-<version>-full.bin
```

esptool v4 (the command is `esptool.py`, and the subcommand has an underscore):

```bash
esptool.py --chip esp32s3 --port COM4 --baud 115200 write_flash 0x0 trackrat-<version>-full.bin
```

The port name depends on your system:

- **Windows:** `COM4`, or whichever COM number Device Manager shows under Ports.
- **macOS:** `/dev/cu.usbmodem...` (list them with `ls /dev/cu.usb*`).
- **Linux:** `/dev/ttyACM0` (you may need to be in the `dialout` group).

If it says "Failed to connect", put the board in download mode (hold **BOOT**, tap **RST**, let go of **BOOT**) and run it again. Press **RST** when it's done.

The full image replaces everything in internal flash, like the browser installer.

## Update without a cable

These two replace the app only. Settings, sessions and the Wi-Fi page and track list in internal flash stay as they are. When a release's notes say the Wi-Fi page or the track list changed, use the browser installer or `uploadfs` (below) instead.

**From the SD card**

1. Copy `trackrat.bin` from the release to the root of the SD card. Don't use `bootloader.bin` or `partitions.bin`.
2. Put the card in and switch on. After the logo, a screen offers to install it. Tap **INSTALL** (or hold BOOT for 1 s). Anything else skips, and so does waiting 30 s.
3. A progress bar shows while it writes (a few seconds). Don't switch it off. Then the board restarts on the new firmware.

The file is renamed `trackrat.installed.bin` afterwards, so it isn't offered again. A file that isn't an ESP32-S3 app image, or is too big, is refused with a message and left on the card. It writes to the second app slot and only switches once the whole image has been written and checked, so a failed write leaves the old firmware running.

**From the Wi-Fi page**

1. Tap **WI-FI** on the menu or on a session summary. The screen shows the network name, the address and an upload code.
2. Join the network **TrackRat-XXXX** on your phone or computer (no password) and open **http://192.168.4.1**.
3. On the **Firmware** tab, choose `trackrat.bin` and enter the upload code. A progress bar shows while it's sent; then Track Rat restarts with Wi-Fi off. Keep it powered until it does.

## Build from source with PlatformIO

Needs [PlatformIO](https://platformio.org/) (VS Code extension or `pip install platformio`).

```bash
pio run -e es3c28p -t upload        # lap timer
```

To copy `data/` into internal flash (the Wi-Fi page, and `tracks.json` for boards without an SD card):

```bash
pio run -e es3c28p -t uploadfs
```

Do this once, and again when the Wi-Fi page changes. It replaces everything in internal flash, including sessions and tracks saved there when there was no SD card.

The board resets itself before and after uploading, so no buttons are needed.

To build the same files a release has, run `python tools/make_release.py` from the repo root. It writes them to `dist/`.

## Troubleshooting

- **The port isn't found.** The cable may be charge-only. Try another cable, ideally one that came with a phone or a data device. Or put the board in download mode: hold **BOOT**, tap **RST**, let go of **BOOT**.
- **The COM port number changes in download mode.** The board shows up as a different port there (for example COM4 normally and COM5 in download mode). Pick the new one.
- **The screen stays dark after installing.** Press **RST**.
- **"Failed to connect".** Hold **BOOT**, tap **RST**, let go of **BOOT**, and run the command again.
