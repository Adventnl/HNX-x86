#!/usr/bin/env python3
"""Decode a USB descriptor blob into human-readable fields.

Input is a hex byte string (whitespace/0x/commas ignored), given as an argument
or on stdin. Handles device, configuration, interface, endpoint and HID
descriptors — the same set the kernel's usb_descriptor.c parses.

    decode_usb_descriptor.py 12 01 00 02 00 00 00 40 27 06 01 00 ...
"""
import sys

DT = {1: "DEVICE", 2: "CONFIGURATION", 3: "STRING", 4: "INTERFACE",
      5: "ENDPOINT", 0x21: "HID", 0x22: "HID_REPORT"}


def u16(b, i):
    return b[i] | (b[i + 1] << 8)


def parse_bytes(text):
    text = text.replace("0x", " ").replace(",", " ")
    return bytes(int(tok, 16) for tok in text.split())


def decode(b):
    off = 0
    while off + 2 <= len(b):
        blen = b[off]
        btype = b[off + 1]
        if blen == 0 or off + blen > len(b):
            print("  [malformed: bLength=%d at off=%d]" % (blen, off))
            break
        name = DT.get(btype, "0x%02X" % btype)
        d = b[off:off + blen]
        print("@%-3d %s (len=%d)" % (off, name, blen))
        if btype == 1 and blen >= 18:
            print("     bcdUSB=0x%04X class=%d sub=%d proto=%d maxPkt0=%d" %
                  (u16(d, 2), d[4], d[5], d[6], d[7]))
            print("     idVendor=0x%04X idProduct=0x%04X numConfigs=%d" %
                  (u16(d, 8), u16(d, 10), d[17]))
        elif btype == 2 and blen >= 9:
            print("     wTotalLength=%d numInterfaces=%d configValue=%d" %
                  (u16(d, 2), d[4], d[5]))
        elif btype == 4 and blen >= 9:
            print("     ifNum=%d numEndpoints=%d class=%d sub=%d proto=%d" %
                  (d[2], d[4], d[5], d[6], d[7]))
        elif btype == 5 and blen >= 7:
            addr = d[2]
            xfer = ["control", "isoch", "bulk", "interrupt"][d[3] & 3]
            print("     epAddr=0x%02X dir=%s type=%s maxPkt=%d interval=%d" %
                  (addr, "IN" if addr & 0x80 else "OUT", xfer, u16(d, 4), d[6]))
        elif btype == 0x21 and blen >= 6:
            print("     bcdHID=0x%04X numDescriptors=%d" % (u16(d, 2), d[5]))
        off += blen


def main():
    arg = " ".join(sys.argv[1:]) if len(sys.argv) > 1 else sys.stdin.read()
    if not arg.strip():
        sys.stderr.write("usage: decode_usb_descriptor.py <hex bytes>\n")
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
