#!/usr/bin/env python3
"""Summarize interrupt-controller bring-up from a captured MyOS serial log.

Greps for the APIC / IOAPIC / IRQ / MSI markers. Offline companion to the in-OS
`interrupts` tool.

    inspect_interrupts.py [logfile]
"""
import os
import re
import sys

DEFAULT = os.path.join("build", "image", "serial.log")

PATTERNS = [
    ("Legacy PIC disabled",    r"\[OK\] Legacy PIC disabled"),
    ("ACPI MADT parsed",       r"\[OK\] ACPI MADT parsed"),
    ("Local APIC enabled",     r"\[OK\] Local APIC enabled"),
    ("IRQ dispatcher online",  r"\[OK\] IRQ dispatcher online"),
    ("LAPIC timer online",     r"\[OK\] Local APIC timer online"),
    ("MSI foundation online",  r"\[OK\] MSI foundation online"),
    ("MSI-X foundation online", r"\[OK\] MSI-X foundation online"),
    ("MSI capability tests",   r"\[PASS\] msi capability tests"),
]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    if not os.path.isfile(path):
        sys.stderr.write("[ERROR] log not found: %s (run a verify-* target first)\n" % path)
        return 1
    text = open(path, "r", errors="replace").read()

    print("=== MyOS interrupt subsystem (from %s) ===" % path)
    for label, pat in PATTERNS:
        ok = re.search(pat, text) is not None
        print("  [%s] %s" % ("x" if ok else " ", label))

    m = re.search(r"msi-capable funcs\s*:\s*0x0*([0-9a-fA-F]+)", text)
    if m:
        print("  MSI-capable PCI functions : %d" % int(m.group(1), 16))
    m = re.search(r"msix-capable funcs\s*:\s*0x0*([0-9a-fA-F]+)", text)
    if m:
        print("  MSI-X-capable PCI functions: %d" % int(m.group(1), 16))
    return 0


if __name__ == "__main__":
    sys.exit(main())
