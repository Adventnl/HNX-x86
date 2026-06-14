#!/usr/bin/env python3
"""Inspect an HNXFS1 image: superblock + root directory listing.

Accepts a raw FS image, or a whole disk image with --offset (bytes) to the
partition that holds the filesystem.
"""
import argparse
import struct
import sys

HNXFS_MAGIC = 0x315346584E48
BLOCK = 4096
INODE_SIZE = 128
DIRENT_SIZE = 128


def main():
    ap = argparse.ArgumentParser(description="Inspect an HNXFS1 image.")
    ap.add_argument("image")
    ap.add_argument("--offset", type=lambda x: int(x, 0), default=0,
                    help="byte offset of the filesystem within the image")
    args = ap.parse_args()

    with open(args.image, "rb") as f:
        f.seek(args.offset)
        data = f.read()

    if len(data) < BLOCK:
        sys.stderr.write("[ERROR] inspect_hnxfs: too small\n")
        return 1
    fields = struct.unpack_from("<QIIQQQQQQQQQ", data, 0)
    (magic, version, block_size, total_blocks, inode_count,
     ibmp, dbmp, itab, itab_blocks, data_start, data_count, root) = fields
    if magic != HNXFS_MAGIC:
        sys.stderr.write("[ERROR] inspect_hnxfs: bad magic 0x%x\n" % magic)
        return 1

    print("HNXFS1 image: %s" % args.image)
    print("  version        : %d" % version)
    print("  block size     : %d" % block_size)
    print("  total blocks   : %d" % total_blocks)
    print("  inode count    : %d" % inode_count)
    print("  inode table    : block %d (%d blocks)" % (itab, itab_blocks))
    print("  data start     : block %d (%d blocks)" % (data_start, data_count))
    print("  root inode     : %d" % root)

    # Root inode -> first data block -> directory entries.
    ioff = itab * BLOCK + 1 * INODE_SIZE
    itype, mode, size, blocks = struct.unpack_from("<IIQQ", data, ioff)
    direct0 = struct.unpack_from("<Q", data, ioff + 24)[0]
    print("  root entries:")
    if direct0:
        base = direct0 * BLOCK
        for s in range(BLOCK // DIRENT_SIZE):
            off = base + s * DIRENT_SIZE
            inode = struct.unpack_from("<Q", data, off)[0]
            if inode == 0:
                continue
            name = data[off + 8: off + 8 + 120].split(b"\x00", 1)[0].decode("utf-8", "replace")
            print("    [%d] %s" % (inode, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
