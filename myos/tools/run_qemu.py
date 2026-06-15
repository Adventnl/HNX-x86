#!/usr/bin/env python3
"""Launch QEMU with OVMF firmware and the MyOS disk image."""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import find_ovmf  # noqa: E402

QEMU = "qemu-system-x86_64"


def main():
    ap = argparse.ArgumentParser(description="Run MyOS in QEMU + OVMF.")
    ap.add_argument("--image", required=True, help="path to myos.img")
    ap.add_argument("--debug", action="store_true",
                    help="start halted with a gdb stub on :1234 (-s -S)")
    ap.add_argument("--headless", action="store_true",
                    help="no graphical window (-display none); serial still on stdio")
    ap.add_argument("--mem", default="256M")
    ap.add_argument("--storage", default=None, help="AHCI/SATA disk image")
    ap.add_argument("--nvme", default=None, help="NVMe disk image")
    args = ap.parse_args()

    if not os.path.isfile(args.image):
        sys.stderr.write("[ERROR] image not found: %s (run 'make image' first)\n" % args.image)
        return 1

    if not shutil.which(QEMU):
        sys.stderr.write(
            "\n[DEP MISSING] %s not found.\n"
            "    macOS: brew install qemu\n"
            "    Debian/Ubuntu: apt-get install qemu-system-x86\n"
            "    Fedora: dnf install qemu-system-x86\n\n" % QEMU
        )
        return 1

    code, vars_template = find_ovmf.find_ovmf()
    if not code:
        # find_ovmf prints its own guidance.
        find_ovmf.main()
        return 1

    image_dir = os.path.dirname(os.path.abspath(args.image))
    if not args.storage:
        cand = os.path.join(image_dir, "storage.img")
        if os.path.isfile(cand):
            args.storage = cand
    if not args.nvme:
        cand = os.path.join(image_dir, "nvme.img")
        if os.path.isfile(cand):
            args.nvme = cand

    cmd = [
        QEMU,
        "-machine", "q35",
        "-m", args.mem,
        "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=%s" % code,
    ]

    # A writable copy of the VARS template, if the firmware uses a split build.
    if vars_template:
        vars_copy = os.path.join(os.path.dirname(os.path.abspath(args.image)), "OVMF_VARS.fd")
        if not os.path.isfile(vars_copy):
            shutil.copyfile(vars_template, vars_copy)
        cmd += ["-drive", "if=pflash,format=raw,unit=1,file=%s" % vars_copy]

    # Kernel serial log (COM1) is mirrored to the terminal via -serial stdio.
    qemu_log = os.path.join(os.path.dirname(os.path.abspath(args.image)), "qemu.log")
    cmd += [
        "-drive", "format=raw,file=%s" % args.image,
    ]
    if args.storage and os.path.isfile(args.storage):
        cmd += [
            "-device", "ich9-ahci,id=ahci0",
            "-drive", "id=hd0,if=none,format=raw,file=%s" % args.storage,
            "-device", "ide-hd,drive=hd0,bus=ahci0.0",
        ]
    if args.nvme and os.path.isfile(args.nvme):
        cmd += [
            "-drive", "id=nvm0,if=none,format=raw,file=%s" % args.nvme,
            "-device", "nvme,serial=hnxnvme,drive=nvm0",
        ]
    # USB: an xHCI host controller with a boot keyboard and mouse attached.
    cmd += [
        "-device", "qemu-xhci,id=xhci",
        "-device", "usb-kbd,bus=xhci.0,id=usbkbd",
        "-device", "usb-mouse,bus=xhci.0,id=usbmouse",
    ]
    cmd += [
        "-serial", "stdio",
        "-net", "none",
        "-no-reboot",
        "-no-shutdown",
        "-d", "guest_errors",
        "-D", qemu_log,
    ]

    if args.headless:
        cmd += ["-display", "none"]

    if args.debug:
        # Halt at reset and expose a gdb stub on tcp::1234.
        cmd += ["-s", "-S"]
        print("[debug] gdb stub on :1234 — connect with:")
        print("        gdb build/kernel/kernel.elf -ex 'target remote :1234'")

    print("[run] " + " ".join(cmd))
    try:
        return subprocess.call(cmd)
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
