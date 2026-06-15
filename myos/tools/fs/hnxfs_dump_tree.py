#!/usr/bin/env python3
"""hnxfs_dump_tree — print the directory tree of an HNXFS1 image.

Walks the filesystem starting at the root inode, following directory entries and
inode block pointers, and prints an indented tree with file sizes. A read-only
inspector that complements inspect_hnxfs.py.

Usage:
    python tools/fs/hnxfs_dump_tree.py <image> [--offset BYTES]
"""
import argparse
import struct
import sys

BLOCK = 4096
INODE_SIZE = 128
DIRENT_SIZE = 128
NAME_MAX = 120
DIRECT = 12
TYPE_FILE, TYPE_DIR = 1, 2
SB_FMT = "<QIIQQQQQQQQQ"
SB_FIELDS = [
    "magic", "version", "block_size", "total_blocks", "inode_count",
    "inode_bitmap_block", "data_bitmap_block", "inode_table_block",
    "inode_table_blocks", "data_block_start", "data_block_count", "root_inode",
]


class Image:
    def __init__(self, data):
        self.data = data
        self.sb = dict(zip(SB_FIELDS, struct.unpack(SB_FMT, data[:struct.calcsize(SB_FMT)])))

    def inode(self, num):
        # Inodes are 1-based; inode `num` lives at index num-1... but the
        # formatter stores the root at table index 1, so use num directly.
        off = self.sb["inode_table_block"] * BLOCK + num * INODE_SIZE
        fmt = "<IIQQ" + "Q" * DIRECT + "Q"
        vals = struct.unpack(fmt, self.data[off:off + struct.calcsize(fmt)])
        return {"type": vals[0], "size": vals[2], "blocks": vals[3],
                "direct": vals[4:4 + DIRECT]}

    def read_block(self, n):
        return self.data[n * BLOCK:(n + 1) * BLOCK]

    def dir_entries(self, ino):
        entries = []
        for b in ino["direct"]:
            if b == 0:
                continue
            blk = self.read_block(b)
            for i in range(BLOCK // DIRENT_SIZE):
                rec = blk[i * DIRENT_SIZE:(i + 1) * DIRENT_SIZE]
                inum = struct.unpack_from("<Q", rec, 0)[0]
                if inum == 0:
                    continue
                name = rec[8:8 + NAME_MAX].split(b"\x00", 1)[0].decode("ascii", "replace")
                entries.append((inum, name))
        return entries


def walk(img, inum, name, depth, seen):
    ino = img.inode(inum)
    indent = "  " * depth
    if ino["type"] == TYPE_DIR:
        print("%s%s/" % (indent, name))
        if inum in seen:
            return
        seen.add(inum)
        for child_inum, child_name in img.dir_entries(ino):
            if child_name in (".", ".."):
                continue
            walk(img, child_inum, child_name, depth + 1, seen)
    else:
        print("%s%s  (%d bytes)" % (indent, name, ino["size"]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--offset", type=int, default=0)
    args = ap.parse_args()
    with open(args.image, "rb") as f:
        f.seek(args.offset)
        data = f.read()
    img = Image(data)
    if img.sb["magic"] != 0x315346584E48:
        print("[FAIL] not an HNXFS image")
        return 1
    print("HNXFS tree of %s:" % args.image)
    walk(img, img.sb["root_inode"], "", 0, set())
    return 0


if __name__ == "__main__":
    sys.exit(main())
