#!/usr/bin/env python3
"""stress_hnxfs — allocation/fragmentation analysis of an HNXFS1 image.

Reads the data and inode bitmaps and reports utilization, the largest free run,
and a simple fragmentation index, then cross-checks the bitmap against the
inode table's referenced blocks (a leaked/double-allocated block is a red flag).
Useful after the kernel fs-stress test writes and deletes many files.

Usage:
    python tools/fs/stress_hnxfs.py <image> [--offset BYTES]
"""
import argparse
import struct
import sys

BLOCK = 4096
INODE_SIZE = 128
DIRECT = 12
SB_FMT = "<QIIQQQQQQQQQ"
SB_FIELDS = [
    "magic", "version", "block_size", "total_blocks", "inode_count",
    "inode_bitmap_block", "data_bitmap_block", "inode_table_block",
    "inode_table_blocks", "data_block_start", "data_block_count", "root_inode",
]


def bits(block_bytes, count):
    out = []
    for i in range(count):
        byte = block_bytes[i // 8]
        out.append((byte >> (i % 8)) & 1)
    return out


def largest_run(bitlist, want_value):
    best = run = 0
    for b in bitlist:
        if b == want_value:
            run += 1
            best = max(best, run)
        else:
            run = 0
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--offset", type=int, default=0)
    args = ap.parse_args()

    with open(args.image, "rb") as f:
        f.seek(args.offset)
        data = f.read()

    sb = dict(zip(SB_FIELDS, struct.unpack(SB_FMT, data[:struct.calcsize(SB_FMT)])))
    if sb["magic"] != 0x315346584E48:
        print("[FAIL] not an HNXFS image")
        return 1

    dbm_off = sb["data_bitmap_block"] * BLOCK
    dbm = data[dbm_off:dbm_off + BLOCK]
    dcount = sb["data_block_count"]
    dbits = bits(dbm, dcount)

    used = sum(dbits)
    free = dcount - used
    util = (100.0 * used / dcount) if dcount else 0.0
    free_run = largest_run(dbits, 0)
    frag = 0.0
    if free > 0:
        frag = 100.0 * (1.0 - (free_run / free))

    print("HNXFS stress/fragmentation report: %s" % args.image)
    print("  total data blocks : %d" % dcount)
    print("  used blocks       : %d (%.1f%%)" % (used, util))
    print("  free blocks       : %d" % free)
    print("  largest free run  : %d blocks" % free_run)
    print("  fragmentation idx : %.1f%%" % frag)

    # Cross-check: blocks referenced by inodes should be marked used.
    itbl = sb["inode_table_block"] * BLOCK
    referenced = set()
    leaked = 0
    for i in range(sb["inode_count"]):
        off = itbl + i * INODE_SIZE
        fmt = "<IIQQ" + "Q" * DIRECT + "Q"
        vals = struct.unpack(fmt, data[off:off + struct.calcsize(fmt)])
        if vals[0] == 0:
            continue
        for b in vals[4:4 + DIRECT]:
            if b:
                referenced.add(b)
                idx = b - sb["data_block_start"]
                if 0 <= idx < dcount and dbits[idx] == 0:
                    leaked += 1
    print("  inode-referenced  : %d blocks" % len(referenced))
    if leaked:
        print("  [WARN] %d referenced blocks not marked used (corruption)" % leaked)
    else:
        print("  [OK] bitmap consistent with inode references")
    return 0


if __name__ == "__main__":
    sys.exit(main())
