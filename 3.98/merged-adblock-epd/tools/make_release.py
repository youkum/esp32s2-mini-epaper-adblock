#!/usr/bin/env python3
"""Merge the PlatformIO build into a single image flashable at 0x0.

PlatformIO keeps the bootloader, the partition table, the OTA selector and
the application as four separate images and flashes each to its own
offset. Many tools - ESP Web Tools in a browser, esptool-js, a friend
with a serial cable - would rather have one file. This produces that
file, using the same offsets the board uses.

Usage:
    python -m platformio run -d .              # build firmware first
    python -m platformio run -d . -t buildfs   # and the blocklist image
    python tools/make_release.py               # -> firmware-merged.bin

The LittleFS image (the compiled blocklist) is included when it exists,
because a device flashed with an empty filesystem blocks nothing and the
dashboard would have nothing to show. Pass --no-fs to leave it out.
"""

import os
import subprocess
import sys

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO_HOME = os.environ.get("PLATFORMIO_CORE_DIR") or os.path.join(
    os.path.expanduser("~"), ".platformio")
BUILD = os.path.join(PROJECT, ".pio", "build", "s2mini")
OUT = os.path.join(PROJECT, "firmware-merged.bin")

# Keep these in step with partitions.csv. The ESP32-S2 bootloader lives at
# 0x1000; the partition table at 0x8000 and the OTA data selector at
# 0xe000 are fixed by ESP-IDF; the application sits at the app0 offset.
LAYOUT = [
    (0x1000, "bootloader.bin"),
    (0x8000, "partitions.bin"),
    (0xE000, os.path.join(PIO_HOME, "packages", "framework-arduinoespressif32",
                          "tools", "partitions", "boot_app0.bin")),
    (0x10000, "firmware.bin"),
    # Keep in step with the littlefs row in partitions.csv.
    (0x250000, "littlefs.bin"),
]
OPTIONAL = {"littlefs.bin"}   # absent until 'pio run -t buildfs' has run


def main():
    esptool = os.path.join(PIO_HOME, "packages", "tool-esptoolpy", "esptool.py")
    if not os.path.isfile(esptool):
        sys.exit("esptool.py not found at %s - run a build first" % esptool)

    args = [
        sys.executable, esptool, "--chip", "esp32s2", "merge_bin",
        "-o", OUT,
        "--flash_mode", "dio",
        "--flash_freq", "40m",
        "--flash_size", "4MB",
    ]
    skip_fs = "--no-fs" in sys.argv
    for offset, path in LAYOUT:
        full = path if os.path.isabs(path) else os.path.join(BUILD, path)
        name = os.path.basename(path)
        if skip_fs and name == "littlefs.bin":
            print("  %#06x  (skipped: --no-fs)" % offset)
            continue
        if not os.path.isfile(full):
            if name in OPTIONAL:
                print("  %#06x  (absent: run 'pio run -t buildfs' to include"
                      " the blocklist)" % offset)
                continue
            sys.exit("missing build output: %s" % full)
        print("  %#06x  %s" % (offset, name))
        args += ["%#x" % offset, full]

    subprocess.check_call(args)
    print("\n%s (%d bytes)" % (OUT, os.path.getsize(OUT)))
    print("flash with:  esptool.py --chip esp32s2 write_flash 0x0 firmware-merged.bin")


if __name__ == "__main__":
    main()
