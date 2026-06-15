#!/usr/bin/env python3
"""generate_network_packets — emit a corpus of synthetic Ethernet/IPv4 packets.

Builds Ethernet+IPv4+UDP/ICMP frames with correct checksums (and a few with
deliberately bad ones) as C arrays or raw binary, to feed the kernel packet
simulation test and offline parser fuzzing. All headers in network byte order.

Usage:
    python tools/test/generate_network_packets.py [--count N] [--format c|bin] [--out F]
"""
import argparse
import struct
import sys


def ip_checksum(data):
    s = 0
    for i in range(0, len(data), 2):
        w = data[i] << 8 | (data[i + 1] if i + 1 < len(data) else 0)
        s += w
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


def eth(dst, src, etype):
    return dst + src + struct.pack(">H", etype)


def ipv4(src, dst, proto, payload, bad=False):
    ver_ihl = 0x45
    total = 20 + len(payload)
    hdr = struct.pack(">BBHHHBBH4s4s", ver_ihl, 0, total, 0x1234, 0x4000,
                      64, proto, 0, src, dst)
    csum = ip_checksum(hdr)
    if bad:
        csum ^= 0xFFFF
    hdr = struct.pack(">BBHHHBBH4s4s", ver_ihl, 0, total, 0x1234, 0x4000,
                      64, proto, csum, src, dst)
    return hdr + payload


def udp(sport, dport, payload):
    length = 8 + len(payload)
    return struct.pack(">HHHH", sport, dport, length, 0) + payload


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=16)
    ap.add_argument("--format", choices=["c", "bin"], default="c")
    ap.add_argument("--out")
    args = ap.parse_args()

    mac_a = bytes([0x52, 0x54, 0x00, 0x12, 0x34, 0x56])
    mac_b = bytes([0x52, 0x54, 0x00, 0x65, 0x43, 0x21])
    frames = []
    for i in range(args.count):
        src = bytes([10, 0, 0, 1 + (i & 0x3F)])
        dst = bytes([10, 0, 0, 2 + (i & 0x3F)])
        payload = ("packet-%d" % i).encode()
        seg = udp(1024 + i, 53, payload)
        bad = (i % 8 == 7)            # every 8th frame has a bad IP checksum
        pkt = eth(mac_b, mac_a, 0x0800) + ipv4(src, dst, 17, seg, bad=bad)
        frames.append(pkt)

    if args.format == "bin":
        blob = b"".join(struct.pack(">H", len(f)) + f for f in frames)
        if args.out:
            open(args.out, "wb").write(blob)
            print("wrote %d frames (%d bytes) to %s" % (len(frames), len(blob), args.out))
        else:
            sys.stdout.buffer.write(blob)
        return 0

    flat = b"".join(frames)
    out = ["/* Generated network packet corpus (Ethernet/IPv4/UDP). */",
           "static const unsigned char net_corpus[] = {"]
    for i in range(0, len(flat), 12):
        out.append("    " + ", ".join("0x%02x" % b for b in flat[i:i + 12]) + ",")
    out.append("};")
    out.append("/* %d frames, %d bytes */" % (len(frames), len(flat)))
    text = "\n".join(out) + "\n"
    if args.out:
        open(args.out, "w").write(text)
        print("wrote %s" % args.out)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
