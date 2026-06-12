#!/usr/bin/env python3
"""Build a bootable FAT UEFI disk image.

Layout produced:
    EFI/BOOT/BOOTX64.EFI
    boot/kernel.elf

Uses mtools (mformat/mmd/mcopy) which works on macOS (brew install mtools) and
Linux without root. Fails with a clear dependency message otherwise.
"""
import argparse
import os
import shutil
import subprocess
import sys


def have(tool):
    return shutil.which(tool) is not None


def run(cmd):
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed: %s\n%s%s\n" % (" ".join(cmd), proc.stdout, proc.stderr))
        return False
    return True


def build_with_mtools(bootloader, kernel, out, size_mb):
    # Create a zeroed image of the requested size.
    with open(out, "wb") as f:
        f.truncate(size_mb * 1024 * 1024)

    steps = [
        ["mformat", "-i", out, "-F", "-v", "MYOS", "::"],
        ["mmd", "-i", out, "::/EFI"],
        ["mmd", "-i", out, "::/EFI/BOOT"],
        ["mmd", "-i", out, "::/boot"],
        ["mcopy", "-i", out, bootloader, "::/EFI/BOOT/BOOTX64.EFI"],
        ["mcopy", "-i", out, kernel, "::/boot/kernel.elf"],
    ]
    for step in steps:
        if not run(step):
            return False
    return True


def main():
    ap = argparse.ArgumentParser(description="Build the MyOS FAT UEFI image.")
    ap.add_argument("--bootloader", required=True, help="path to BOOTX64.EFI")
    ap.add_argument("--kernel", required=True, help="path to kernel.elf")
    ap.add_argument("--out", required=True, help="output image path")
    ap.add_argument("--size-mb", type=int, default=64, help="image size in MiB")
    args = ap.parse_args()

    for label, path in (("bootloader", args.bootloader), ("kernel", args.kernel)):
        if not os.path.isfile(path):
            sys.stderr.write("[ERROR] missing %s: %s (run 'make all' first)\n" % (label, path))
            return 1

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)

    if not (have("mformat") and have("mmd") and have("mcopy")):
        sys.stderr.write(
            "\n[DEP MISSING] mtools (mformat/mmd/mcopy) not found.\n"
            "  These are required to build the FAT image without root.\n"
            "    macOS: brew install mtools\n"
            "    Debian/Ubuntu: apt-get install mtools\n"
            "    Fedora: dnf install mtools\n\n"
        )
        return 1

    if not build_with_mtools(args.bootloader, args.kernel, args.out, args.size_mb):
        sys.stderr.write("[ERROR] image build failed.\n")
        return 1

    print("[OK] image written: %s (%d MiB FAT)" % (args.out, args.size_mb))
    print("      EFI/BOOT/BOOTX64.EFI")
    print("      boot/kernel.elf")
    return 0


if __name__ == "__main__":
    sys.exit(main())
