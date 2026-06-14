#!/usr/bin/env python3
"""Inspect / validate a MyOS HXF1 initramfs archive.

Prints the header and entry table. With --require <path> (repeatable), exits
non-zero unless every required archive path is present — used by
`make verify-initramfs` to confirm the archive contains all /bin, /tests and
/etc files without booting QEMU.
"""
import argparse
import struct
import sys

HXF_MAGIC = 0x31465848          # "HXF1"
HXF_HDR_SIZE = 16
HXF_ENTRY_SIZE = 152
HXF_PATH_MAX = 128


def fail(msg):
    sys.stderr.write("[ERROR] inspect_initramfs: %s\n" % msg)
    sys.exit(1)


def parse(data):
    if len(data) < HXF_HDR_SIZE:
        fail("file too small for an HXF header")
    magic, version, count, hdr_size = struct.unpack_from("<IIII", data, 0)
    if magic != HXF_MAGIC:
        fail("bad magic 0x%08x (expected HXF1)" % magic)
    entries = []
    off = hdr_size
    for i in range(count):
        if off + HXF_ENTRY_SIZE > len(data):
            fail("entry %d out of bounds" % i)
        raw_path = data[off:off + HXF_PATH_MAX]
        path = raw_path.split(b"\x00", 1)[0].decode("utf-8", "replace")
        e_off, e_size, e_flags = struct.unpack_from("<QQQ", data, off + HXF_PATH_MAX)
        if e_off + e_size > len(data):
            fail("blob for %s out of bounds" % path)
        entries.append((path, e_off, e_size, e_flags))
        off += HXF_ENTRY_SIZE
    return version, entries


def main():
    ap = argparse.ArgumentParser(description="Inspect a MyOS HXF1 initramfs.")
    ap.add_argument("archive")
    ap.add_argument("--require", action="append", default=[],
                    help="archive path that must be present (repeatable)")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    with open(args.archive, "rb") as f:
        data = f.read()

    version, entries = parse(data)
    names = {e[0] for e in entries}

    if not args.quiet:
        print("HXF1 archive: %s" % args.archive)
        print("  version : %d" % version)
        print("  entries : %d" % len(entries))
        print("  size    : %d bytes" % len(data))
        for path, off, size, flags in entries:
            aligned = "ok" if off % 8 == 0 else "UNALIGNED"
            print("    %-28s off=%-8d size=%-8d (%s)" % (path, off, size, aligned))

    missing = [p for p in args.require if p not in names]
    if missing:
        sys.stderr.write("[FAIL] initramfs missing required files:\n")
        for m in missing:
            sys.stderr.write("        - %s\n" % m)
        return 1

    if args.require:
        print("[PASS] initramfs contains all %d required files" % len(args.require))
    return 0


if __name__ == "__main__":
    sys.exit(main())
