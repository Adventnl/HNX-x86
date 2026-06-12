/* HXF1 initramfs: a tiny, custom read-only archive format (not tar/cpio/zip).
 *
 * Layout in memory (all little-endian, 8-byte aligned):
 *   struct hxf_header                       (16 bytes)
 *   struct hxf_entry  x entry_count         (152 bytes each)
 *   file blobs (referenced by entry.offset, relative to the archive base)
 *
 * The bootloader loads \boot\initramfs.hxf into RAM (EfiLoaderData, which the
 * PMM never frees and the VMM identity-maps) and hands the kernel its physical
 * base + size via boot_info. */
#ifndef MYOS_INITRAMFS_H
#define MYOS_INITRAMFS_H

#include "types.h"

#define HXF_MAGIC   0x31465848U      /* "HXF1" little-endian */
#define HXF_VERSION 1U
#define HXF_PATH_MAX 128

struct hxf_header {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t header_size;            /* sizeof(struct hxf_header) */
};

struct hxf_entry {
    char     path[HXF_PATH_MAX];
    uint64_t offset;                 /* byte offset of the blob from archive base */
    uint64_t size;                   /* blob size in bytes */
    uint64_t flags;                  /* reserved (0) */
};

/* Validate and record the archive at [base, base+size). Logs
 * "[OK] Initramfs loaded" and "[OK] Initramfs parsed" on success. */
void initramfs_init(uint64_t base, uint64_t size);

/* 1 if a valid archive is available, 0 otherwise. */
int initramfs_is_available(void);

/* Look up an absolute archive path (e.g. "/bin/init.hxe"). Returns a pointer
 * into the archive (read-only) and writes the blob size to *out_size, or NULL
 * if not found / unavailable. */
const void *initramfs_find(const char *path, uint64_t *out_size);

/* Log every entry (path + size) for diagnostics. */
void initramfs_dump(void);

#endif /* MYOS_INITRAMFS_H */
