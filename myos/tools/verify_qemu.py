#!/usr/bin/env python3
"""Headless QEMU boot verifier.

Runs the image in QEMU + OVMF with no graphical window, captures the COM1
serial output, and checks that every --expect marker appears within --timeout
seconds. Prints [PASS]/[FAIL] and stores the captured log under build/image/.
"""
import argparse
import os
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import find_ovmf  # noqa: E402

QEMU = "qemu-system-x86_64"


def dep_missing(tool, hint):
    sys.stderr.write('\n[DEP MISSING] required tool "%s" not found.\n  %s\n\n' % (tool, hint))


def main():
    ap = argparse.ArgumentParser(description="Verify a MyOS boot in QEMU headlessly.")
    ap.add_argument("--image", required=True)
    ap.add_argument("--timeout", type=int, default=40)
    ap.add_argument("--mem", default="256M")
    ap.add_argument("--expect", action="append", default=[],
                    help="required serial substring (repeatable)")
    ap.add_argument("--log", default=None, help="captured serial log path")
    ap.add_argument("--test-name", default="verify")
    ap.add_argument("--storage", default=None, help="AHCI/SATA disk image to attach")
    ap.add_argument("--nvme", default=None, help="NVMe disk image to attach")
    args = ap.parse_args()

    if not os.path.isfile(args.image):
        sys.stderr.write("[FAIL] %s: image not found: %s\n" % (args.test_name, args.image))
        return 1
    if not shutil.which(QEMU):
        dep_missing(QEMU, "macOS: brew install qemu   Debian/Ubuntu: apt-get install qemu-system-x86")
        return 1
    code, vars_template = find_ovmf.find_ovmf()
    if not code:
        find_ovmf.main()
        return 1

    image_dir = os.path.dirname(os.path.abspath(args.image))
    log_path = args.log or os.path.join(image_dir, "%s.log" % args.test_name)

    # Auto-attach storage/NVMe images sitting next to myos.img if not specified.
    if not args.storage:
        cand = os.path.join(image_dir, "storage.img")
        if os.path.isfile(cand):
            args.storage = cand
    if not args.nvme:
        cand = os.path.join(image_dir, "nvme.img")
        if os.path.isfile(cand):
            args.nvme = cand

    cmd = [
        QEMU, "-machine", "q35", "-m", args.mem,
        "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=%s" % code,
    ]
    if vars_template:
        vars_copy = os.path.join(image_dir, "OVMF_VARS_%s.fd" % args.test_name)
        shutil.copyfile(vars_template, vars_copy)
        cmd += ["-drive", "if=pflash,format=raw,unit=1,file=%s" % vars_copy]
    cmd += [
        "-drive", "format=raw,file=%s" % args.image,
    ]
    # Attach a SATA disk on a dedicated AHCI controller (deterministic bus name).
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
    cmd += [
        "-serial", "stdio",
        "-display", "none",
        "-net", "none",
        "-no-reboot", "-no-shutdown",
    ]

    print("[%s] mem=%s timeout=%ds" % (args.test_name, args.mem, args.timeout))
    print("[run] " + " ".join(cmd))

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            bufsize=1, universal_newlines=True)

    captured = []
    lock = threading.Lock()

    def reader():
        for line in proc.stdout:
            with lock:
                captured.append(line)

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    remaining = set(args.expect)
    deadline = time.time() + args.timeout
    while time.time() < deadline:
        if proc.poll() is not None:
            break
        with lock:
            text = "".join(captured)
        remaining = {m for m in args.expect if m not in text}
        if not remaining:
            break
        time.sleep(0.2)

    # Stop QEMU.
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
    t.join(timeout=2)

    with lock:
        text = "".join(captured)
    with open(log_path, "w") as f:
        f.write(text)

    remaining = [m for m in args.expect if m not in text]
    if remaining:
        print("[FAIL] %s — missing markers:" % args.test_name)
        for m in remaining:
            print("        - %r" % m)
        print("        (serial log: %s)" % log_path)
        return 1

    print("[PASS] %s — all %d markers found (log: %s)" %
          (args.test_name, len(args.expect), log_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
