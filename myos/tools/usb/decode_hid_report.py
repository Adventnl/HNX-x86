#!/usr/bin/env python3
"""Decode a HID report descriptor (short items) into a readable item list.

Mirrors the kernel's hid_report.c walker. Input is a hex byte string as an
argument or on stdin.

    decode_hid_report.py 05 01 09 06 a1 01 ...
"""
import sys

ITEM_TYPE = {0: "Main", 1: "Global", 2: "Local"}
MAIN = {0x8: "Input", 0x9: "Output", 0xB: "Feature", 0xA: "Collection",
        0xC: "EndCollection"}
GLOBAL = {0x0: "UsagePage", 0x1: "LogicalMin", 0x2: "LogicalMax", 0x7: "ReportSize",
          0x8: "ReportID", 0x9: "ReportCount"}
LOCAL = {0x0: "Usage", 0x1: "UsageMin", 0x2: "UsageMax"}
USAGE_PAGE = {0x01: "Generic Desktop", 0x07: "Keyboard/Keypad", 0x09: "Button",
              0x0C: "Consumer"}


def parse_bytes(text):
    text = text.replace("0x", " ").replace(",", " ")
    return bytes(int(tok, 16) for tok in text.split())


def item_data(b, i, size):
    n = 4 if size == 3 else size
    v = 0
    for k in range(n):
        v |= b[i + k] << (8 * k)
    return v, n


def decode(b):
    off = 0
    total_bits = 0
    rsize = rcount = 0
    while off < len(b):
        prefix = b[off]
        off += 1
        size = prefix & 3
        typ = (prefix >> 2) & 3
        tag = (prefix >> 4) & 0xF
        if off + (4 if size == 3 else size) > len(b):
            print("  [truncated item]")
            break
        data, n = item_data(b, off, size)
        tname = ITEM_TYPE.get(typ, "?")
        if typ == 0:
            label = MAIN.get(tag, "0x%X" % tag)
        elif typ == 1:
            label = GLOBAL.get(tag, "0x%X" % tag)
        else:
            label = LOCAL.get(tag, "0x%X" % tag)
        extra = ""
        if typ == 1 and tag == 0x0:
            extra = " (%s)" % USAGE_PAGE.get(data, "0x%X" % data)
            rsize = rsize
        if typ == 1 and tag == 0x7:
            rsize = data
        if typ == 1 and tag == 0x9:
            rcount = data
        if typ == 0 and tag == 0x8:
            total_bits += rsize * rcount
        print("  %-7s %-13s = %d%s" % (tname, label, data, extra))
        off += n
    print("total input report size: %d bits (%d bytes)" % (total_bits, (total_bits + 7) // 8))


def main():
    arg = " ".join(sys.argv[1:]) if len(sys.argv) > 1 else sys.stdin.read()
    if not arg.strip():
        sys.stderr.write("usage: decode_hid_report.py <hex bytes>\n")
        return 2
    try:
        b = parse_bytes(arg)
    except ValueError as e:
        sys.stderr.write("[ERROR] bad hex input: %s\n" % e)
        return 1
    decode(b)
    return 0


if __name__ == "__main__":
    sys.exit(main())
