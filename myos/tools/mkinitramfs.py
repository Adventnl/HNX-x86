#!/usr/bin/env python3
"""Pack files into the MyOS HXF1 initramfs archive (not tar/cpio/zip).

Layout (little-endian):
    struct hxf_header { u32 magic; u32 version; u32 entry_count; u32 header_size; } (16 b)
    struct hxf_entry  { char path[128]; u64 offset; u64 size; u64 flags; } x N      (152 b)
    file blobs (referenced by entry.offset, relative to the archive start)

Usage:
    mkinitramfs.py --out build/image/initramfs.hxf \
        /bin/init.hxe build/user/init.hxe \
        /bin/syscall_test.hxe build/user/syscall_test.hxe \
        /etc/banner.txt user/init/banner.txt
(positional args are (archive_path host_file) pairs).
"""
import argparse
import os
import struct
import sys

HXF_MAGIC = 0x31465848          # "HXF1"
HXF_VERSION = 1
HXF_HDR_SIZE = 16
HXF_ENTRY_SIZE = 152
HXF_PATH_MAX = 128


def fail(msg):
    sys.stderr.write("[ERROR] mkinitramfs: %s\n" % msg)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description="Build a MyOS HXF1 initramfs.")
    ap.add_argument("--out", required=True, help="output .hxf path")
    ap.add_argument("pairs", nargs="+",
                    help="(archive_path host_file) pairs, e.g. /bin/init.hxe build/user/init.hxe")
    args = ap.parse_args()

    if len(args.pairs) % 2 != 0:
        fail("expected an even number of (archive_path host_file) arguments")

    files = []
    for i in range(0, len(args.pairs), 2):
        arch_path = args.pairs[i]
        host_path = args.pairs[i + 1]
        if not os.path.isfile(host_path):
            fail("missing input file: %s" % host_path)
        pb = arch_path.encode("utf-8")
        if len(pb) >= HXF_PATH_MAX:
            fail("archive path too long (>= %d): %s" % (HXF_PATH_MAX, arch_path))
        with open(host_path, "rb") as f:
            blob = f.read()
        files.append((pb, blob))

    n = len(files)
    blob_base = HXF_HDR_SIZE + n * HXF_ENTRY_SIZE

    entries = bytearray()
    blobs = bytearray()
    cursor = blob_base
    for pb, blob in files:
        path_field = pb + b"\x00" * (HXF_PATH_MAX - len(pb))
        entries += path_field
        entries += struct.pack("<QQQ", cursor, len(blob), 0)
        blobs += blob
        cursor += len(blob)

    header = struct.pack("<IIII", HXF_MAGIC, HXF_VERSION, n, HXF_HDR_SIZE)
    out = bytes(header) + bytes(entries) + bytes(blobs)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out)

    print("[OK] HXF1 written: %s (%d entries, %d bytes)" % (args.out, n, len(out)))
    for pb, blob in files:
        print("      %-24s %d bytes" % (pb.decode("utf-8"), len(blob)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
