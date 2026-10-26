"""
PlatformIO post-build script (extra_scripts in platformio.ini).

Sets the project_name in the app descriptor (esp_app_desc_t) to "trackrat".

The Arduino core's libraries are prebuilt, so the descriptor says "arduino-lib-builder": the same as
any other Arduino program for this chip. The Wi-Fi page and the SD card installer refuse a firmware
file whose project_name isn't "trackrat" (include/FirmwareImage.h), so this has to be set. It is
patched into firmware.elf just before esp-idf's elf2image turns it into firmware.bin, which then
takes the elf's hash and checksums over the new name.

esp_app_desc_t: magic 0xABCD5432 (4), secure_version (4), reserv1 (8), version (32), project_name (32).
"""

Import("env")  # noqa: F821 (provided by PlatformIO)

NAME = b"trackrat"
MAGIC = bytes([0x32, 0x54, 0xCD, 0xAB])
NAME_AT = 48  # from the magic
NAME_LEN = 32
KNOWN_BEFORE = (b"arduino-lib-builder", NAME)


def set_app_name(source, target, env):
    elf = str(source[0])
    with open(elf, "rb") as f:
        data = bytearray(f.read())
    # The descriptor is the first thing in .rodata_desc; take the one whose name field is a name we know
    at = data.find(MAGIC)
    while at >= 0:
        field = bytes(data[at + NAME_AT: at + NAME_AT + NAME_LEN])
        if field.rstrip(b"\0") in KNOWN_BEFORE:
            break
        at = data.find(MAGIC, at + 1)
    if at < 0:
        raise SystemExit("set_app_name: the app descriptor wasn't found in " + elf)
    new = NAME + bytes(NAME_LEN - len(NAME))
    if bytes(data[at + NAME_AT: at + NAME_AT + NAME_LEN]) != new:
        data[at + NAME_AT: at + NAME_AT + NAME_LEN] = new
        with open(elf, "wb") as f:
            f.write(data)
        print("set_app_name: project_name is now trackrat")


env.AddPreAction("$BUILD_DIR/${PROGNAME}.bin", set_app_name)
