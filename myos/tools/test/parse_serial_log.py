#!/usr/bin/env python3
"""parse_serial_log — extract and summarize test markers from a serial log.

Scans a QEMU serial capture for [OK]/[PASS]/[FAIL]/[PANIC]/[WARN] markers and
prints a categorized summary. Exit code is the number of [FAIL]/[PANIC] lines so
it can gate CI.

Usage:
    python tools/test/parse_serial_log.py <serial.log> [--require MARKER ...]
"""
import argparse
import sys

CATS = ["[PASS]", "[OK]", "[FAIL]", "[PANIC]", "[WARN]", "[ERROR]"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--require", action="append", default=[],
                    help="a substring that must appear (repeatable)")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    counts = {c: 0 for c in CATS}
    passes, fails = [], []
    try:
        with open(args.log, "r", errors="replace") as f:
            lines = f.readlines()
    except OSError as e:
        print("cannot read %s: %s" % (args.log, e))
        return 2

    for line in lines:
        for c in CATS:
            if c in line:
                counts[c] += 1
                if c == "[PASS]":
                    passes.append(line.strip())
                elif c in ("[FAIL]", "[PANIC]"):
                    fails.append(line.strip())

    if not args.quiet:
        print("=== serial log summary: %s ===" % args.log)
        for c in CATS:
            print("  %-9s %d" % (c, counts[c]))
        if fails:
            print("--- failures ---")
            for f in fails:
                print("  " + f)

    missing = []
    blob = "".join(lines)
    for req in args.require:
        if req not in blob:
            missing.append(req)
    if missing:
        print("[FAIL] missing required markers:")
        for m in missing:
            print("  - " + m)
        return len(missing) + len(fails)

    rc = counts["[FAIL]"] + counts["[PANIC]"]
    print("[%s] %d pass / %d fail" % ("OK" if rc == 0 else "FAIL",
                                      counts["[PASS]"], rc))
    return rc


if __name__ == "__main__":
    sys.exit(main())
