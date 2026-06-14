#!/usr/bin/env python3
"""Minimal PCI class/vendor name lookup (mirrors kernel/pci/pci_ids.c).

Not the full pci.ids database — a curated subset used for diagnostics.

    pci_ids_min.py --class 01 --subclass 06     -> storage:sata-ahci
    pci_ids_min.py --vendor 8086                 -> Intel
"""
import argparse
import sys

CLASSES = {
    (0x01, 0x01): "storage:ide",
    (0x01, 0x06): "storage:sata-ahci",
    (0x01, 0x08): "storage:nvme",
    (0x02, None): "network",
    (0x03, None): "display",
    (0x06, 0x00): "bridge:host",
    (0x06, 0x01): "bridge:isa",
    (0x06, 0x04): "bridge:pci",
    (0x0C, 0x03): "serial:usb",
}

VENDORS = {
    0x8086: "Intel", 0x1022: "AMD", 0x1234: "QEMU/Bochs",
    0x1AF4: "Red Hat/Virtio", 0x1B36: "Red Hat", 0x10EC: "Realtek",
    0x15AD: "VMware",
}


def class_name(cls, sub):
    if (cls, sub) in CLASSES:
        return CLASSES[(cls, sub)]
    if (cls, None) in CLASSES:
        return CLASSES[(cls, None)]
    return "device"


def main():
    ap = argparse.ArgumentParser(description="PCI class/vendor name lookup.")
    ap.add_argument("--class", dest="cls", type=lambda x: int(x, 16))
    ap.add_argument("--subclass", type=lambda x: int(x, 16), default=0)
    ap.add_argument("--vendor", type=lambda x: int(x, 16))
    args = ap.parse_args()

    if args.vendor is not None:
        print(VENDORS.get(args.vendor, "unknown"))
        return 0
    if args.cls is not None:
        print(class_name(args.cls, args.subclass))
        return 0
    # No args: dump the tables.
    for (c, s), name in CLASSES.items():
        print("class %02x:%s  %s" % (c, "%02x" % s if s is not None else "**", name))
    for v, name in VENDORS.items():
        print("vendor %04x  %s" % (v, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
