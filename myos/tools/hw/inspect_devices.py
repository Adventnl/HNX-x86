#!/usr/bin/env python3
"""Summarize the hardware inventory from a captured MyOS serial log.

Greps a boot log (default build/image/serial.log) for the PCI / xHCI / USB / HID
markers the kernel prints and reports what was discovered. Offline companion to
the in-OS `hwinfo`/`devtree` tools.

    inspect_devices.py [logfile]
"""
import os
import re
import sys

DEFAULT = os.path.join("build", "image", "serial.log")

PATTERNS = [
    ("PCI bus scanned",        r"\[OK\] PCI bus scanned"),
    ("PCI capabilities",       r"\[OK\] PCI capabilities parsed"),
    ("MSI foundation",         r"\[OK\] MSI foundation online"),
    ("MSI-X foundation",       r"\[OK\] MSI-X foundation online"),
    ("xHCI controller",        r"\[OK\] xHCI controller found"),
    ("xHCI started",           r"\[OK\] xHCI controller started"),
    ("xHCI root hub",          r"\[OK\] xHCI root hub scanned"),
    ("USB core",               r"\[OK\] USB core online"),
    ("USB keyboard",           r"\[OK\] USB keyboard online"),
    ("USB mouse",              r"\[OK\] USB mouse online"),
    ("Unified input",          r"\[OK\] Unified input stack online"),
]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    if not os.path.isfile(path):
        sys.stderr.write("[ERROR] log not found: %s (run a verify-* target first)\n" % path)
        return 1
    text = open(path, "r", errors="replace").read()

    print("=== MyOS hardware inventory (from %s) ===" % path)
    for label, pat in PATTERNS:
        ok = re.search(pat, text) is not None
        print("  [%s] %s" % ("x" if ok else " ", label))

    m = re.search(r"usb enumerated\s*:\s*0x0*([0-9a-fA-F]+)", text)
    if m:
        print("  usb devices enumerated: %d" % int(m.group(1), 16))
    ports = re.findall(r"xhci port up:\s*0x0*([0-9a-fA-F]+)", text)
    if ports:
        print("  active root ports: %s" % ", ".join(str(int(p, 16)) for p in ports))
    return 0


if __name__ == "__main__":
    sys.exit(main())
