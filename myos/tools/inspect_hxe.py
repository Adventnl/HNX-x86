#!/usr/bin/env python3
"""Inspect a MyOS HXE1 user executable: header + segment table."""
import argparse
import struct
import sys

HXE_MAGIC = 0x31455848          # "HXE1"
HXE_HDR_SIZE = 32
HXE_SEG_SIZE = 40

FLAG_NAMES = [(1, "R"), (2, "W"), (4, "X")]


def fail(msg):
    sys.stderr.write("[ERROR] inspect_hxe: %s\n" % msg)
    sys.exit(1)


def flags_str(f):
    return "".join(name for bit, name in FLAG_NAMES if f & bit) or "-"


def main():
    ap = argparse.ArgumentParser(description="Inspect a MyOS HXE1 executable.")
    ap.add_argument("hxe")
    args = ap.parse_args()

    with open(args.hxe, "rb") as f:
        data = f.read()
    if len(data) < HXE_HDR_SIZE:
        fail("file too small for an HXE header")

    magic, version, entry, seg_count, hdr_size = struct.unpack_from("<IIQQQ", data, 0)
    if magic != HXE_MAGIC:
        fail("bad magic 0x%08x (expected HXE1)" % magic)

    print("HXE1 executable: %s" % args.hxe)
    print("  version       : %d" % version)
    print("  entry         : 0x%x" % entry)
    print("  segments      : %d" % seg_count)
    print("  size          : %d bytes" % len(data))
    off = hdr_size
    for i in range(seg_count):
        if off + HXE_SEG_SIZE > len(data):
            fail("segment %d out of bounds" % i)
        vaddr, memsz, filesz, foff, flags = struct.unpack_from("<QQQQQ", data, off)
        print("    seg %d: vaddr=0x%-10x memsz=%-7d filesz=%-7d off=%-7d [%s]"
              % (i, vaddr, memsz, filesz, foff, flags_str(flags)))
        off += HXE_SEG_SIZE
    return 0


if __name__ == "__main__":
    sys.exit(main())
