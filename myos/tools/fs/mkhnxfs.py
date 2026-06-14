#!/usr/bin/env python3
"""Format a raw HNXFS1 filesystem image (matches kernel/fs/hnxfs/hnxfs_format.h).

Layout (4 KiB blocks):
    block 0   superblock
    block 1   inode bitmap
    block 2   data bitmap
    block 3.. inode table (32 inodes / block)
    ...       data blocks (root directory lives in the first one)
"""
import argparse
import struct
import sys

HNXFS_MAGIC = 0x315346584E48        # "HNXFS1"
HNXFS_VERSION = 1
BLOCK = 4096
INODE_SIZE = 128
INODES_PER_BLOCK = BLOCK // INODE_SIZE
DIRENT_SIZE = 128
NAME_MAX = 120
DIRECT = 12

TYPE_DIR = 2

INODE_TABLE_BLOCKS = 8              # 256 inodes


def pack_superblock(total_blocks, data_start, data_count, inode_count):
    return struct.pack(
        "<QIIQQQQQQQQQ",
        HNXFS_MAGIC, HNXFS_VERSION, BLOCK,
        total_blocks, inode_count,
        1,                          # inode_bitmap_block
        2,                          # data_bitmap_block
        3,                          # inode_table_block
        INODE_TABLE_BLOCKS,         # inode_table_blocks
        data_start, data_count,
        1,                          # root_inode
    )


def pack_inode(itype, size, blocks, direct):
    direct = (list(direct) + [0] * DIRECT)[:DIRECT]
    return struct.pack("<IIQQ" + "Q" * DIRECT + "Q",
                       itype, 0o755, size, blocks, *direct, 0)


def pack_dirent(inode, name):
    nb = name.encode("utf-8")[:NAME_MAX - 1]
    return struct.pack("<Q", inode) + nb + b"\x00" * (NAME_MAX - len(nb))


def main():
    ap = argparse.ArgumentParser(description="Format an HNXFS1 image.")
    ap.add_argument("--out", required=True)
    ap.add_argument("--size-mb", type=int, default=8)
    args = ap.parse_args()

    total_blocks = (args.size_mb * 1024 * 1024) // BLOCK
    data_start = 3 + INODE_TABLE_BLOCKS
    if total_blocks <= data_start + 1:
        sys.stderr.write("[ERROR] mkhnxfs: image too small\n")
        return 1
    data_count = total_blocks - data_start
    inode_count = INODE_TABLE_BLOCKS * INODES_PER_BLOCK

    img = bytearray(total_blocks * BLOCK)

    # Superblock.
    sb = pack_superblock(total_blocks, data_start, data_count, inode_count)
    img[0:len(sb)] = sb

    # inode bitmap: inode 0 reserved, inode 1 = root.
    img[1 * BLOCK + 0] = 0b00000011

    # data bitmap: data block 0 used (root directory).
    img[2 * BLOCK + 0] = 0b00000001

    # root inode (number 1): a directory with one data block.
    root = pack_inode(TYPE_DIR, BLOCK, 1, [data_start])
    img[3 * BLOCK + 1 * INODE_SIZE: 3 * BLOCK + 2 * INODE_SIZE] = root

    # root directory data block: "." and ".." both point at root (inode 1).
    rd = data_start * BLOCK
    img[rd: rd + DIRENT_SIZE] = pack_dirent(1, ".")
    img[rd + DIRENT_SIZE: rd + 2 * DIRENT_SIZE] = pack_dirent(1, "..")

    with open(args.out, "wb") as f:
        f.write(img)

    print("[OK] HNXFS image: %s (%d blocks, %d inodes, %d data blocks)"
          % (args.out, total_blocks, inode_count, data_count))
    return 0


if __name__ == "__main__":
    sys.exit(main())
