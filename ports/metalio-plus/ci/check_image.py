#!/usr/bin/env python3
"""Sanity-check an app image before it is offered for flashing to the Plus app1 slot."""
import struct
import sys
from pathlib import Path

APP_SLOT_SIZE = 0x640000  # app0/app1 on the RickyOS-Plus partition table
LARGE_APP1_SIZE = 0x9A0000  # app1 in partitions-large-app1.csv
ESP32S31_CHIP_ID = 32


def main() -> int:
    data = Path(sys.argv[1]).read_bytes()
    if len(data) < 0x120 or data[0] != 0xE9:
        print("not an ESP app image", file=sys.stderr)
        return 1
    chip = struct.unpack("<H", data[12:14])[0]
    if chip != ESP32S31_CHIP_ID:
        print(f"image chip id {chip}, expected {ESP32S31_CHIP_ID} (ESP32-S31)", file=sys.stderr)
        return 1
    if len(data) > LARGE_APP1_SIZE:
        print(f"image is {len(data)} bytes, even the large app1 holds {LARGE_APP1_SIZE}", file=sys.stderr)
        return 1
    fits_stock = len(data) <= APP_SLOT_SIZE
    desc = data[0x20:0x120]
    magic = struct.unpack("<I", desc[:4])[0]
    version = desc[16:48].split(b"\0")[0].decode(errors="replace")
    project = desc[48:80].split(b"\0")[0].decode(errors="replace")
    idf = desc[112:144].split(b"\0")[0].decode(errors="replace")
    if magic != 0xABCD5432:
        print("missing app descriptor", file=sys.stderr)
        return 1
    print(f"OK: {project} {version}, IDF {idf}, {len(data)} bytes, chip ESP32-S31; "
          f"{100 * len(data) / APP_SLOT_SIZE:.1f}% of stock app1, "
          f"{100 * len(data) / LARGE_APP1_SIZE:.1f}% of large app1 -> "
          + ("fits the stock layout" if fits_stock else "NEEDS partitions-large-app1"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
