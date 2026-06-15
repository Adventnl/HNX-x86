#!/usr/bin/env python3
"""corrupt_hnxfs — deliberately damage an HNXFS1 image to exercise fsck.

A test/QA helper: flips a chosen superblock field (or a random data block) so
that fsck.hnxfs (and the in-kernel corruption-detection test) have something to
catch. Writes to a copy unless --in-place is given.

Usage:
    python tools/fs/corrupt_hnxfs.py <image> [--out OUT] [--what FIELD]
"""
import argparse
import struct
import sys

MAGIC = 0x315346584E48
BLOCK = 4096

# (struct offset, struct format) for each corruptible superblock field.
FIELDS = {
    "magic":           (0,  "<Q"),
    "version":         (8,  "<I"),
    "block_size":      (12, "<I"),
    "total_blocks":    (16, "<Q"),
    "root_inode":      (88, "<Q"),
    "data_block_start":(72, "<Q"),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--out", help="output path (default: <image>.corrupt)")
    ap.add_argument("--what", default="magic", choices=list(FIELDS.keys()),
                    help="which superblock field to corrupt")
    ap.add_argument("--value", type=lambda v: int(v, 0), default=0xDEADBEEF,
                    help="replacement value")
    ap.add_argument("--in-place", action="store_true")
    args = ap.parse_args()

    with open(args.image, "rb") as f:
        data = bytearray(f.read())

    off, fmt = FIELDS[args.what]
    size = struct.calcsize(fmt)
    old = struct.unpack_from(fmt, data, off)[0]
    struct.pack_into(fmt, data, off, args.value & ((1 << (8 * size)) - 1))
    print("corrupted %s: 0x%X -> 0x%X (offset %d)" % (args.what, old, args.value, off))

    out = args.image if args.in_place else (args.out or args.image + ".corrupt")
    with open(out, "wb") as f:
        f.write(data)
    print("wrote %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
