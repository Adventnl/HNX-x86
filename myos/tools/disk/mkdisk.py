#!/usr/bin/env python3
"""Build a raw MBR disk image with two partitions:
    p1 = HNXFS filesystem (from --part1), p2 = scratch (zeroed).

The kernel's MBR parser exposes these as disk0p1 / disk0p2.
"""
import argparse
import os
import struct
import sys

SECTOR = 512
ALIGN = 2048                 # 1 MiB partition alignment


def align_up(x, a):
    return (x + a - 1) // a * a


def part_entry(boot, ptype, start_lba, count):
    return struct.pack("<B3sB3sII", boot, b"\x00\x00\x00", ptype,
                       b"\x00\x00\x00", start_lba, count)


def main():
    ap = argparse.ArgumentParser(description="Build a 2-partition MBR disk image.")
    ap.add_argument("--out", required=True)
    ap.add_argument("--part1", required=True, help="HNXFS image for partition 1")
    ap.add_argument("--scratch-mb", type=int, default=1, help="partition 2 size (MiB)")
    args = ap.parse_args()

    if not os.path.isfile(args.part1):
        sys.stderr.write("[ERROR] mkdisk: missing %s\n" % args.part1)
        return 1
    with open(args.part1, "rb") as f:
        fs = f.read()

    p1_start = ALIGN
    p1_count = align_up(len(fs), SECTOR) // SECTOR
    p2_start = align_up(p1_start + p1_count, ALIGN)
    p2_count = args.scratch_mb * (1024 * 1024 // SECTOR)
    total_sectors = align_up(p2_start + p2_count, ALIGN)

    img = bytearray(total_sectors * SECTOR)

    # MBR partition table.
    img[446:462] = part_entry(0x00, 0x83, p1_start, p1_count)
    img[462:478] = part_entry(0x00, 0x83, p2_start, p2_count)
    img[510] = 0x55
    img[511] = 0xAA

    # Place the filesystem image into partition 1.
    img[p1_start * SECTOR: p1_start * SECTOR + len(fs)] = fs

    with open(args.out, "wb") as f:
        f.write(img)

    print("[OK] disk image: %s (%d MiB)" % (args.out, total_sectors * SECTOR // (1024 * 1024)))
    print("      p1 hnxfs   start=%d count=%d" % (p1_start, p1_count))
    print("      p2 scratch start=%d count=%d" % (p2_start, p2_count))
    return 0


if __name__ == "__main__":
    sys.exit(main())
