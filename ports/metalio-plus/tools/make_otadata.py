#!/usr/bin/env python3
"""Write an 8 KiB otadata image that boots a given app slot on the Metalio Plus.

Usage: make_otadata.py <slot 0|1> <current max ota_seq> <out.bin>
The bootloader boots the valid entry with the highest ota_seq; slot = (seq - 1) % 2.
The CRC is esp_rom_crc32_le(UINT32_MAX, &ota_seq, 4), which equals
zlib.crc32(seq_bytes, 0xFFFFFFFF) (checked against otadata read from a Plus).
"""
import struct, sys, zlib

def crc(seq: int) -> int:
    return zlib.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF

def entry(seq: int) -> bytes:
    return struct.pack("<I", seq) + b"\xff" * 20 + struct.pack("<I", 0xFFFFFFFF) + struct.pack("<I", crc(seq))

def main():
    slot, cur = int(sys.argv[1]), int(sys.argv[2])
    seq = cur + 1
    while (seq - 1) % 2 != slot:
        seq += 1
    sector = entry(seq).ljust(0x1000, b"\xff")
    open(sys.argv[3], "wb").write(sector + b"\xff" * 0x1000)
    print(f"otadata: seq={seq} -> app{slot}")

if __name__ == "__main__":
    main()
