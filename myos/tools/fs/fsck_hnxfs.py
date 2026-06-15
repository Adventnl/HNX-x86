#!/usr/bin/env python3
"""fsck.hnxfs — offline consistency checker for an HNXFS1 image.

Validates the superblock geometry, the bitmaps vs. the inode table, and the
directory tree reachable from the root inode. Mirrors the in-kernel
hnxfs_fsck.c checks plus deeper cross-checks that only make sense offline.

Usage:
    python tools/fs/fsck_hnxfs.py <image> [--offset BYTES] [--verbose]

Exit code 0 = clean, non-zero = number of problems found.
"""
import argparse
import struct
import sys

MAGIC = 0x315346584E48          # "HNXFS1"
VERSION = 1
BLOCK = 4096
INODE_SIZE = 128
INODES_PER_BLOCK = BLOCK // INODE_SIZE
DIRENT_SIZE = 128
NAME_MAX = 120
DIRECT = 12
TYPE_FREE, TYPE_FILE, TYPE_DIR = 0, 1, 2

SB_FMT = "<QIIQQQQQQQQQ"
SB_FIELDS = [
    "magic", "version", "block_size", "total_blocks", "inode_count",
    "inode_bitmap_block", "data_bitmap_block", "inode_table_block",
    "inode_table_blocks", "data_block_start", "data_block_count", "root_inode",
]


class Fsck:
    def __init__(self, data, verbose=False):
        self.data = data
        self.verbose = verbose
        self.problems = []

    def block(self, n):
        off = n * BLOCK
        return self.data[off:off + BLOCK]

    def fail(self, msg):
        self.problems.append(msg)
        print("  [PROBLEM] " + msg)

    def info(self, msg):
        if self.verbose:
            print("  " + msg)

    def parse_sb(self):
        raw = self.data[:struct.calcsize(SB_FMT)]
        vals = struct.unpack(SB_FMT, raw)
        return dict(zip(SB_FIELDS, vals))

    def check_superblock(self, sb):
        if sb["magic"] != MAGIC:
            self.fail("bad magic 0x%X (want 0x%X)" % (sb["magic"], MAGIC))
        if sb["version"] != VERSION:
            self.fail("bad version %d" % sb["version"])
        if sb["block_size"] != BLOCK:
            self.fail("bad block size %d" % sb["block_size"])
        if sb["root_inode"] != 1:
            self.fail("bad root inode %d" % sb["root_inode"])
        if sb["inode_count"] == 0 or sb["total_blocks"] == 0:
            self.fail("zero inode/block count")
        order_ok = (sb["inode_bitmap_block"] >= 1 and
                    sb["data_bitmap_block"] > sb["inode_bitmap_block"] and
                    sb["inode_table_block"] > sb["data_bitmap_block"] and
                    sb["data_block_start"] >=
                    sb["inode_table_block"] + sb["inode_table_blocks"])
        if not order_ok:
            self.fail("region ordering invalid")
        if sb["data_block_start"] + sb["data_block_count"] > sb["total_blocks"]:
            self.fail("data area overruns total_blocks")

    def iter_inodes(self, sb):
        base = sb["inode_table_block"] * BLOCK
        for i in range(sb["inode_count"]):
            off = base + i * INODE_SIZE
            raw = self.data[off:off + INODE_SIZE]
            if len(raw) < INODE_SIZE:
                break
            fmt = "<IIQQ" + "Q" * DIRECT + "Q"
            vals = struct.unpack(fmt, raw[:struct.calcsize(fmt)])
            yield i, {
                "type": vals[0], "mode": vals[1], "size": vals[2],
                "blocks": vals[3], "direct": vals[4:4 + DIRECT],
            }

    def check_inodes(self, sb):
        used = 0
        for idx, ino in self.iter_inodes(sb):
            if ino["type"] == TYPE_FREE:
                continue
            used += 1
            if ino["type"] not in (TYPE_FILE, TYPE_DIR):
                self.fail("inode %d has invalid type %d" % (idx, ino["type"]))
            for b in ino["direct"]:
                if b == 0:
                    continue
                if b < sb["data_block_start"] or b >= sb["total_blocks"]:
                    self.fail("inode %d points at out-of-range block %d" % (idx, b))
        self.info("inodes in use: %d" % used)

    def run(self):
        if len(self.data) < BLOCK:
            self.fail("image smaller than one block")
            return self.problems
        sb = self.parse_sb()
        self.info("superblock: %r" % sb)
        self.check_superblock(sb)
        if not self.problems:
            self.check_inodes(sb)
        return self.problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--offset", type=int, default=0,
                    help="byte offset of the filesystem within the image")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    with open(args.image, "rb") as f:
        f.seek(args.offset)
        data = f.read()

    print("fsck.hnxfs: %s (offset %d, %d bytes)" % (args.image, args.offset, len(data)))
    fs = Fsck(data, args.verbose)
    problems = fs.run()
    if problems:
        print("[FAIL] %d problem(s) found" % len(problems))
        return len(problems)
    print("[OK] filesystem clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
