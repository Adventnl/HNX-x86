#!/usr/bin/env python3
"""generate_fs_stress — emit a deterministic filesystem stress workload.

Produces a list of filesystem operations (create/write/read/truncate/rename/
unlink) as a simple line-based script that a kernel or userland driver can
replay. Deterministic from a seed so failures reproduce. Useful for feeding the
fs-stress test and for offline image fuzzing.

Usage:
    python tools/test/generate_fs_stress.py [--count N] [--seed S] [--out FILE]
"""
import argparse
import sys


class Rng:
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFFFFFFFFFF

    def next(self):
        x = self.s
        x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
        x ^= x >> 7
        x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
        self.s = x
        return x


OPS = ["create", "write", "read", "truncate", "rename", "unlink"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=200)
    ap.add_argument("--seed", type=int, default=0xC0FFEE)
    ap.add_argument("--files", type=int, default=16)
    ap.add_argument("--out")
    args = ap.parse_args()

    rng = Rng(args.seed)
    lines = []
    for _ in range(args.count):
        op = OPS[rng.next() % len(OPS)]
        fid = rng.next() % args.files
        path = "/disk/stress_%02d.bin" % fid
        if op == "write":
            size = 1 + (rng.next() % 8192)
            lines.append("write %s %d" % (path, size))
        elif op == "truncate":
            size = rng.next() % 4096
            lines.append("truncate %s %d" % (path, size))
        elif op == "rename":
            dst = "/disk/stress_%02d.bin" % (rng.next() % args.files)
            lines.append("rename %s %s" % (path, dst))
        else:
            lines.append("%s %s" % (op, path))

    text = "\n".join(lines) + "\n"
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
        print("wrote %d ops to %s" % (len(lines), args.out))
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
