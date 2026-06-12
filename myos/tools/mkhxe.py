#!/usr/bin/env python3
"""Convert a linked user ELF64 into the MyOS HXE1 executable format.

HXE1 is a minimal custom load format (not ELF): the kernel loads it instead of
parsing ELF in ring 0. We read the ELF64 program headers, keep only PT_LOAD
segments, and emit:

    struct hxe_header  { u32 magic; u32 version; u64 entry;
                         u64 segment_count; u64 header_size; }   (32 bytes)
    struct hxe_segment { u64 virtual_address; u64 memory_size;
                         u64 file_size; u64 file_offset; u64 flags; } x N (40 b)
    segment file bytes (file_size each, in segment order)

Rules enforced: only PT_LOAD; file_size <= memory_size; all addresses below
USER_TOP and at/above USER_IMAGE_BASE; segments must not overlap (page granular);
the entry point must land inside an executable segment.
"""
import argparse
import struct
import sys

HXE_MAGIC = 0x31455848          # "HXE1"
HXE_VERSION = 1
HXE_HDR_SIZE = 32
HXE_SEG_SIZE = 40

HXE_SEG_READ = 1
HXE_SEG_WRITE = 2
HXE_SEG_EXEC = 4

PT_LOAD = 1
PF_X, PF_W, PF_R = 1, 2, 4

USER_IMAGE_BASE = 0x0000000000400000
USER_TOP = 0x0000800000000000
PAGE = 0x1000


def fail(msg):
    sys.stderr.write("[ERROR] mkhxe: %s\n" % msg)
    sys.exit(1)


def page_down(x):
    return x & ~(PAGE - 1)


def page_up(x):
    return (x + PAGE - 1) & ~(PAGE - 1)


def parse_elf(data):
    if len(data) < 64 or data[:4] != b"\x7fELF":
        fail("not an ELF file")
    if data[4] != 2:
        fail("not ELF64")
    if data[5] != 1:
        fail("not little-endian")
    (e_type, e_machine) = struct.unpack_from("<HH", data, 16)
    if e_type != 2:
        fail("not ET_EXEC (need a non-PIE static executable)")
    if e_machine != 62:
        fail("not x86-64")
    e_entry = struct.unpack_from("<Q", data, 24)[0]
    e_phoff = struct.unpack_from("<Q", data, 32)[0]
    e_phentsize = struct.unpack_from("<H", data, 54)[0]
    e_phnum = struct.unpack_from("<H", data, 56)[0]
    if e_phoff == 0 or e_phnum == 0:
        fail("no program headers")

    segs = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack_from("<II", data, off)
        p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = \
            struct.unpack_from("<QQQQQQ", data, off + 8)
        if p_type != PT_LOAD:
            continue
        segs.append({
            "vaddr": p_vaddr, "memsz": p_memsz, "filesz": p_filesz,
            "offset": p_offset, "flags": p_flags,
        })
    if not segs:
        fail("no PT_LOAD segments")
    return e_entry, segs


def hxe_flags(p_flags):
    f = 0
    if p_flags & PF_R:
        f |= HXE_SEG_READ
    if p_flags & PF_W:
        f |= HXE_SEG_WRITE
    if p_flags & PF_X:
        f |= HXE_SEG_EXEC
    return f


def main():
    ap = argparse.ArgumentParser(description="Convert a user ELF64 to HXE1.")
    ap.add_argument("elf", help="input linked user ELF")
    ap.add_argument("out", help="output .hxe path")
    args = ap.parse_args()

    with open(args.elf, "rb") as f:
        data = f.read()

    entry, segs = parse_elf(data)

    # Validate.
    segs.sort(key=lambda s: s["vaddr"])
    for s in segs:
        if s["filesz"] > s["memsz"]:
            fail("segment file_size > memory_size")
        if s["vaddr"] < USER_IMAGE_BASE:
            fail("segment below USER_IMAGE_BASE (0x%x)" % s["vaddr"])
        if s["vaddr"] + s["memsz"] > USER_TOP:
            fail("segment crosses USER_TOP")
    for i in range(1, len(segs)):
        prev = segs[i - 1]
        cur = segs[i]
        if page_up(prev["vaddr"] + prev["memsz"]) > page_down(cur["vaddr"]):
            fail("overlapping segments")

    exec_segs = [s for s in segs if (s["flags"] & PF_X)]
    if not any(s["vaddr"] <= entry < s["vaddr"] + s["memsz"] for s in exec_segs):
        fail("entry 0x%x not inside an executable segment" % entry)

    # Lay out the file: header, segment table, then blobs.
    n = len(segs)
    blob_base = HXE_HDR_SIZE + n * HXE_SEG_SIZE
    seg_records = bytearray()
    blobs = bytearray()
    cursor = blob_base
    for s in segs:
        fsz = s["filesz"]
        foff = cursor if fsz else 0
        seg_records += struct.pack("<QQQQQ", s["vaddr"], s["memsz"], fsz, foff,
                                   hxe_flags(s["flags"]))
        if fsz:
            blobs += data[s["offset"]:s["offset"] + fsz]
            cursor += fsz

    header = struct.pack("<IIQQQ", HXE_MAGIC, HXE_VERSION, entry, n, HXE_HDR_SIZE)
    out = bytes(header) + bytes(seg_records) + bytes(blobs)
    with open(args.out, "wb") as f:
        f.write(out)

    print("[OK] HXE1 written: %s (entry=0x%x, %d segment(s), %d bytes)"
          % (args.out, entry, n, len(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
