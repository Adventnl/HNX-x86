#!/usr/bin/env python3
"""generate_usb_descriptors — emit a corpus of USB descriptor blobs.

Generates device + configuration descriptor byte blobs (valid and edge-case) as
C arrays or raw binary, to feed the kernel USB descriptor parser's corpus test
and offline fuzzing. Little-endian (USB wire order == x86 native).

Usage:
    python tools/test/generate_usb_descriptors.py [--count N] [--format c|bin] [--out F]
"""
import argparse
import struct
import sys

DT_DEVICE = 0x01
DT_CONFIG = 0x02
DT_INTERFACE = 0x04
DT_ENDPOINT = 0x05


def device_desc(vid, pid, dclass):
    return struct.pack("<BBHBBBBHHHBBBB",
                       18, DT_DEVICE, 0x0200, dclass, 0, 0, 64,
                       vid, pid, 0x0100, 1, 2, 3, 1)


def config_desc(num_if):
    iface = struct.pack("<BBBBBBBBB", 9, DT_INTERFACE, 0, 0, 1, 3, 1, 1, 0)
    ep = struct.pack("<BBBBHB", 7, DT_ENDPOINT, 0x81, 0x03, 8, 10)
    total = 9 + len(iface) + len(ep)
    cfg = struct.pack("<BBHBBBBB", 9, DT_CONFIG, total, num_if, 1, 0, 0xA0, 50)
    return cfg + iface + ep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=16)
    ap.add_argument("--format", choices=["c", "bin"], default="c")
    ap.add_argument("--out")
    args = ap.parse_args()

    blobs = []
    for i in range(args.count):
        blobs.append(device_desc(0x1000 + i, 0x2000 + i, (i * 7) & 0xFF))
        blobs.append(config_desc(1 + (i & 3)))

    if args.format == "bin":
        data = b"".join(blobs)
        if args.out:
            open(args.out, "wb").write(data)
            print("wrote %d bytes to %s" % (len(data), args.out))
        else:
            sys.stdout.buffer.write(data)
        return 0

    out = []
    out.append("/* Generated USB descriptor corpus. */")
    out.append("static const unsigned char usb_corpus[] = {")
    flat = b"".join(blobs)
    for i in range(0, len(flat), 12):
        row = ", ".join("0x%02x" % b for b in flat[i:i + 12])
        out.append("    " + row + ",")
    out.append("};")
    out.append("/* %d descriptors, %d bytes */" % (len(blobs), len(flat)))
    text = "\n".join(out) + "\n"
    if args.out:
        open(args.out, "w").write(text)
        print("wrote %s" % args.out)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
