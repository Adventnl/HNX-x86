#!/usr/bin/env python3
"""Inspect an MBR disk image: print the partition table."""
import argparse
import struct
import sys


def main():
    ap = argparse.ArgumentParser(description="Inspect an MBR disk image.")
    ap.add_argument("image")
    args = ap.parse_args()

    with open(args.image, "rb") as f:
        mbr = f.read(512)
    if len(mbr) < 512 or mbr[510] != 0x55 or mbr[511] != 0xAA:
        sys.stderr.write("[ERROR] inspect_disk: no valid MBR signature\n")
        return 1

    print("MBR disk: %s" % args.image)
    print("  %-4s %-6s %-12s %-12s" % ("part", "type", "start_lba", "sectors"))
    for i in range(4):
        e = mbr[446 + i * 16: 446 + i * 16 + 16]
        ptype = e[4]
        start, count = struct.unpack_from("<II", e, 8)
        if ptype == 0 or count == 0:
            continue
        print("  p%-3d 0x%02x   %-12d %-12d" % (i + 1, ptype, start, count))
    return 0


if __name__ == "__main__":
    sys.exit(main())
