#!/usr/bin/env python3
"""
Capture the board's serial log without accidentally resetting it.

    python tools/serial_log.py --port COM4 --seconds 20
    python tools/serial_log.py --port COM4 --seconds 20 --reset    # restart the board first

Most serial tools switch DTR/RTS on when they open a port. On the ESP32-S3's
built-in USB that resets the chip, often into download mode, where it sits
doing nothing until RST is pressed. This script keeps both off.

--reset restarts the board normally (no buttons needed) and reconnects, so
the log starts from boot. The USB port disappears for a moment during the
restart, so the first line or two can be missed.

Needs pyserial (installed with PlatformIO, or: pip install pyserial).
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("This needs pyserial: pip install pyserial")


def open_quiet(port, baud, wait_s=8.0):
    """Open the port with DTR/RTS off, retrying while USB re-enumerates."""
    deadline = time.time() + wait_s
    while True:
        try:
            s = serial.Serial()
            s.port = port
            s.baudrate = baud
            s.timeout = 0.2
            s.dtr = False  # set before open() so they are never asserted
            s.rts = False
            s.open()
            return s
        except (serial.SerialException, OSError):
            if time.time() > deadline:
                raise
            time.sleep(0.1)


def restart_board(port, baud):
    """Normal reset through the USB serial/JTAG controller: RTS pulse with DTR off.

    Windows' usbser.sys only sends the board a line-state change when DTR is set, so DTR is set
    again after each RTS change (as esptool does); without it the RTS pulse never arrives.
    """
    s = open_quiet(port, baud)
    s.dtr = False
    s.rts = True
    s.dtr = False
    time.sleep(0.1)
    s.rts = False
    s.dtr = False
    s.close()
    time.sleep(0.3)  # let the USB port drop before reconnecting


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="e.g. COM4 or /dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=20)
    ap.add_argument("--reset", action="store_true", help="restart the board first, to capture start-up")
    args = ap.parse_args()

    if args.reset:
        restart_board(args.port, args.baud)

    s = open_quiet(args.port, args.baud)
    end = time.time() + args.seconds
    buf = b""
    try:
        while time.time() < end:
            try:
                chunk = s.read(512)
            except serial.SerialException:  # board restarted: reconnect
                s.close()
                s = open_quiet(args.port, args.baud)
                continue
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    print(line.decode("utf-8", "replace").rstrip("\r"), flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        if buf:
            print(buf.decode("utf-8", "replace"))
        s.close()


if __name__ == "__main__":
    main()
