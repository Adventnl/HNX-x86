#!/usr/bin/env python3
"""run_matrix — drive a matrix of `make verify-*` targets and summarize.

Runs each requested verification target (default: the production set), captures
pass/fail, and prints a table. A thin orchestration layer over the Makefile so
the whole production suite can be launched and summarized from one command.

Usage:
    python tools/test/run_matrix.py [--targets t1 t2 ...] [--make make]
"""
import argparse
import subprocess
import sys

DEFAULT_TARGETS = [
    "verify-kernel-core-expanded",
    "verify-process-expanded",
    "verify-syscall-expanded",
    "verify-vfs-expanded",
    "verify-hnxfs-expanded",
    "verify-page-cache",
    "verify-fs-stress",
    "verify-network",
    "verify-libc-expanded",
    "verify-shell-expanded",
    "verify-coreutils-expanded",
    "verify-services",
    "verify-test-infra",
    "verify-stress",
    "verify-randomized",
]


def run_target(make, target):
    try:
        p = subprocess.run([make, target], capture_output=True, text=True,
                           timeout=600)
        out = p.stdout + p.stderr
        ok = (p.returncode == 0) and ("[PASS]" in out or "passed" in out)
        return ok, out
    except Exception as e:  # noqa: BLE001
        return False, str(e)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--targets", nargs="*", default=DEFAULT_TARGETS)
    ap.add_argument("--make", default="make")
    args = ap.parse_args()

    results = []
    for t in args.targets:
        ok, _ = run_target(args.make, t)
        results.append((t, ok))
        print("  %-32s %s" % (t, "PASS" if ok else "FAIL"))

    passed = sum(1 for _, ok in results if ok)
    total = len(results)
    print("=== %d/%d targets passed ===" % (passed, total))
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
