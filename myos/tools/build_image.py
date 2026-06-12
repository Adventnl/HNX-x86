#!/usr/bin/env python3
"""Build a bootable FAT UEFI disk image.

Layout produced (whole-disk FAT volume, as OVMF/QEMU expects for removable
media):
    EFI/BOOT/BOOTX64.EFI
    boot/kernel.elf
    boot/initramfs.hxf        (Prompt 4)

Two backends:
  * mtools (mformat/mmd/mcopy) when present — used on macOS/Linux.
  * a built-in pure-Python FAT16 writer otherwise (no external dependency, and
    it emits VFAT long-name entries so "initramfs.hxf" resolves correctly).

Either backend yields the same directory tree.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys


# --------------------------------------------------------------------------- #
#  mtools backend                                                             #
# --------------------------------------------------------------------------- #
def have(tool):
    return shutil.which(tool) is not None


def run(cmd):
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed: %s\n%s%s\n" % (" ".join(cmd), proc.stdout, proc.stderr))
        return False
    return True


def build_with_mtools(files, out, size_mb):
    with open(out, "wb") as f:
        f.truncate(size_mb * 1024 * 1024)
    steps = [
        ["mformat", "-i", out, "-F", "-v", "MYOS", "::"],
        ["mmd", "-i", out, "::/EFI"],
        ["mmd", "-i", out, "::/EFI/BOOT"],
        ["mmd", "-i", out, "::/boot"],
    ]
    for arch_path, host_path in files:
        steps.append(["mcopy", "-i", out, host_path, "::" + arch_path])
    for step in steps:
        if not run(step):
            return False
    return True


# --------------------------------------------------------------------------- #
#  Pure-Python FAT16 backend (with VFAT long names)                           #
# --------------------------------------------------------------------------- #
BPS = 512                       # bytes per sector
FIXED_DATE = 0x5821             # 2024-01-01
FIXED_TIME = 0x0000


class Node:
    def __init__(self, name, is_dir, data=b""):
        self.name = name
        self.is_dir = is_dir
        self.data = data
        self.children = []
        self.first = 0          # first cluster (0 = none/empty)
        self.nclusters = 0


def lfn_checksum(short11):
    s = 0
    for c in short11:
        s = (((s & 1) << 7) + (s >> 1) + c) & 0xFF
    return s


def make_short_name(name, used):
    """Return an 11-byte 8.3 short name unique within `used`."""
    name = name.rstrip(".")
    if "." in name:
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""

    def clean(s):
        out = ""
        for ch in s.upper():
            if ch.isalnum() or ch in "_~-":
                out += ch
        return out

    base = clean(base) or "FILE"
    ext = clean(ext)[:3]

    candidate = base[:8]
    if candidate not in used and len(base) <= 8 and (("." + ext if ext else "")):
        short = candidate
    else:
        n = 1
        while True:
            tag = "~%d" % n
            short = base[:8 - len(tag)] + tag
            if short not in used:
                break
            n += 1
    used.add(short)
    name11 = (short.ljust(8) + ext.ljust(3)).encode("ascii")
    return name11


def lfn_entries(longname, short11):
    """Build the VFAT long-name 32-byte entries (in on-disk reverse order)."""
    csum = lfn_checksum(short11)
    u = longname.encode("utf-16-le")
    chars = [u[i:i + 2] for i in range(0, len(u), 2)]
    chars.append(b"\x00\x00")           # terminating NUL
    # Pad to a multiple of 13 with 0xFFFF.
    while len(chars) % 13 != 0:
        chars.append(b"\xff\xff")
    total = len(chars) // 13
    entries = []
    for seq in range(total):
        part = chars[seq * 13:(seq + 1) * 13]
        order = seq + 1
        if seq == total - 1:
            order |= 0x40                # last logical entry
        e = bytearray()
        e += bytes([order])
        e += b"".join(part[0:5])
        e += bytes([0x0F, 0x00, csum])
        e += b"".join(part[5:11])
        e += b"\x00\x00"
        e += b"".join(part[11:13])
        entries.append(bytes(e))
    entries.reverse()                    # on disk: highest seq first
    return entries


def dir_entry(name11, attr, first_cluster, size):
    return struct.pack("<11sBBBHHHHHHHI",
                       name11, attr, 0, 0,
                       FIXED_TIME, FIXED_DATE, FIXED_DATE,
                       (first_cluster >> 16) & 0xFFFF,
                       FIXED_TIME, FIXED_DATE,
                       first_cluster & 0xFFFF, size)


def build_dir_bytes(node, is_root):
    """Serialize a directory's 32-byte records (children + . / .. for subdirs)."""
    out = bytearray()
    used = set()
    if not is_root:
        out += dir_entry(b".          ", 0x10, node.first, 0)
        parent_first = node.parent.first if node.parent and not node.parent.is_root else 0
        out += dir_entry(b"..         ", 0x10, parent_first, 0)
    for child in node.children:
        short11 = make_short_name(child.name, used)
        for e in lfn_entries(child.name, short11):
            out += e
        if child.is_dir:
            out += dir_entry(short11, 0x10, child.first, 0)
        else:
            out += dir_entry(short11, 0x20, child.first, len(child.data))
    return bytes(out)


def build_with_fat16(files, out, size_mb):
    total_sectors = (size_mb * 1024 * 1024) // BPS
    reserved = 1
    num_fats = 2
    root_entries = 512
    root_dir_sectors = (root_entries * 32 + BPS - 1) // BPS

    # Pick a cluster size that keeps the count inside the FAT16 range.
    spc = 1
    while spc <= 64:
        tmp1 = total_sectors - (reserved + root_dir_sectors)
        tmp2 = (256 * spc) + num_fats
        fatsz = (tmp1 + tmp2 - 1) // tmp2
        data_sectors = total_sectors - (reserved + num_fats * fatsz + root_dir_sectors)
        clusters = data_sectors // spc
        if 4085 <= clusters < 65525:
            break
        spc *= 2
    else:
        sys.stderr.write("[ERROR] could not pick FAT16 geometry for %d MiB\n" % size_mb)
        return False

    cluster_bytes = spc * BPS

    # Build the tree.
    root = Node("", True)
    root.parent = None
    root.is_root = True
    dirs = {"": root}

    def get_dir(path):
        if path in dirs:
            return dirs[path]
        parent_path, _, name = path.rpartition("/")
        parent = get_dir(parent_path)
        d = Node(name, True)
        d.parent = parent
        d.is_root = False
        parent.children.append(d)
        dirs[path] = d
        return d

    file_nodes = []
    for arch_path, host_path in files:
        with open(host_path, "rb") as f:
            data = f.read()
        dir_path, _, fname = arch_path.lstrip("/").rpartition("/")
        d = get_dir(dir_path)
        fn = Node(fname, False, data)
        fn.parent = d
        fn.is_root = False
        d.children.append(fn)
        file_nodes.append(fn)

    # Allocate clusters: files, then non-root dirs (sizes are child-count based).
    next_cluster = 2

    def alloc(nclusters):
        nonlocal next_cluster
        first = next_cluster
        next_cluster += nclusters
        return first

    for fn in file_nodes:
        n = max(1, (len(fn.data) + cluster_bytes - 1) // cluster_bytes) if fn.data else 0
        if n:
            fn.first = alloc(n)
            fn.nclusters = n

    subdirs = [d for p, d in dirs.items() if p != ""]
    for d in subdirs:
        nentries = 2 + sum(1 + len(lfn_entries(c.name, b"X" * 11)) for c in d.children)
        nbytes = nentries * 32
        n = max(1, (nbytes + cluster_bytes - 1) // cluster_bytes)
        d.first = alloc(n)
        d.nclusters = n

    cluster_count = next_cluster - 2
    if cluster_count >= clusters:
        sys.stderr.write("[ERROR] content exceeds image capacity\n")
        return False

    # FAT: entry 0/1 reserved; each node's chain links its clusters then EOC.
    total_fat_entries = clusters + 2
    fat = [0] * total_fat_entries
    fat[0] = 0xFFF8
    fat[1] = 0xFFFF

    def chain(first, n):
        for i in range(n):
            c = first + i
            fat[c] = (first + i + 1) if i < n - 1 else 0xFFFF

    for fn in file_nodes:
        if fn.nclusters:
            chain(fn.first, fn.nclusters)
    for d in subdirs:
        chain(d.first, d.nclusters)

    # Cluster data region.
    data_region = bytearray(clusters * cluster_bytes)

    def write_cluster_chain(first, n, payload):
        buf = payload + b"\x00" * (n * cluster_bytes - len(payload))
        off = (first - 2) * cluster_bytes
        data_region[off:off + len(buf)] = buf

    for fn in file_nodes:
        if fn.nclusters:
            write_cluster_chain(fn.first, fn.nclusters, fn.data)
    for d in subdirs:
        write_cluster_chain(d.first, d.nclusters, build_dir_bytes(d, False))

    # Root directory region (fixed, not cluster-based).
    root_bytes = build_dir_bytes(root, True)
    root_region = bytearray(root_dir_sectors * BPS)
    root_region[:len(root_bytes)] = root_bytes

    # Boot sector / BPB.
    boot = bytearray(BPS)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"MSWIN4.1"
    struct.pack_into("<H", boot, 11, BPS)
    boot[13] = spc
    struct.pack_into("<H", boot, 14, reserved)
    boot[16] = num_fats
    struct.pack_into("<H", boot, 17, root_entries)
    if total_sectors < 0x10000:
        struct.pack_into("<H", boot, 19, total_sectors)
    else:
        struct.pack_into("<H", boot, 19, 0)
        struct.pack_into("<I", boot, 32, total_sectors)
    boot[21] = 0xF8                       # media descriptor
    struct.pack_into("<H", boot, 22, fatsz)
    struct.pack_into("<H", boot, 24, 32)  # sectors per track
    struct.pack_into("<H", boot, 26, 2)   # heads
    boot[36] = 0x80                       # drive number
    boot[38] = 0x29                       # extended boot signature
    struct.pack_into("<I", boot, 39, 0x12345678)
    boot[43:54] = b"MYOS       "
    boot[54:62] = b"FAT16   "
    boot[510] = 0x55
    boot[511] = 0xAA

    # FAT serialized as little-endian 16-bit entries, padded to fatsz sectors.
    fat_bytes = bytearray()
    for e in fat:
        fat_bytes += struct.pack("<H", e & 0xFFFF)
    fat_region = bytearray(fatsz * BPS)
    fat_region[:len(fat_bytes)] = fat_bytes

    # Assemble the whole image.
    image = bytearray(total_sectors * BPS)
    pos = 0
    image[pos:pos + BPS] = boot
    pos = reserved * BPS
    for _ in range(num_fats):
        image[pos:pos + len(fat_region)] = fat_region
        pos += len(fat_region)
    image[pos:pos + len(root_region)] = root_region
    pos += len(root_region)
    image[pos:pos + len(data_region)] = data_region

    with open(out, "wb") as f:
        f.write(image)
    return True


# --------------------------------------------------------------------------- #
def main():
    ap = argparse.ArgumentParser(description="Build the MyOS FAT UEFI image.")
    ap.add_argument("--bootloader", required=True, help="path to BOOTX64.EFI")
    ap.add_argument("--kernel", required=True, help="path to kernel.elf")
    ap.add_argument("--initramfs", default=None, help="path to initramfs.hxf")
    ap.add_argument("--out", required=True, help="output image path")
    ap.add_argument("--size-mb", type=int, default=64, help="image size in MiB")
    args = ap.parse_args()

    files = [
        ("/EFI/BOOT/BOOTX64.EFI", args.bootloader),
        ("/boot/kernel.elf", args.kernel),
    ]
    if args.initramfs:
        files.append(("/boot/initramfs.hxf", args.initramfs))

    for arch_path, path in files:
        if not os.path.isfile(path):
            sys.stderr.write("[ERROR] missing %s: %s (run 'make all'/'make initramfs' first)\n"
                             % (arch_path, path))
            return 1

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)

    if have("mformat") and have("mmd") and have("mcopy"):
        ok = build_with_mtools(files, args.out, args.size_mb)
        backend = "mtools"
    else:
        ok = build_with_fat16(files, args.out, args.size_mb)
        backend = "builtin FAT16"

    if not ok:
        sys.stderr.write("[ERROR] image build failed (%s backend).\n" % backend)
        return 1

    print("[OK] image written: %s (%d MiB FAT, %s)" % (args.out, args.size_mb, backend))
    for arch_path, _ in files:
        print("      %s" % arch_path.lstrip("/"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
