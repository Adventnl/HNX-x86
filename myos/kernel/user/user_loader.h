/* HXE1 user executable format + loader.
 *
 * HXE1 is a minimal custom load format (not ELF): a header, a segment table,
 * and the segment file bytes. mkhxe.py produces it from a linked user ELF by
 * flattening its PT_LOAD program headers. */
#ifndef MYOS_USER_LOADER_H
#define MYOS_USER_LOADER_H

#include "types.h"

struct user_address_space;

#define HXE_MAGIC   0x31455848U      /* "HXE1" little-endian */
#define HXE_VERSION 1U

#define HXE_SEG_READ  1ULL
#define HXE_SEG_WRITE 2ULL
#define HXE_SEG_EXEC  4ULL

struct hxe_header {
    uint32_t magic;
    uint32_t version;
    uint64_t entry;
    uint64_t segment_count;
    uint64_t header_size;            /* sizeof(struct hxe_header) */
};

struct hxe_segment {
    uint64_t virtual_address;
    uint64_t memory_size;
    uint64_t file_size;
    uint64_t file_offset;            /* byte offset of segment bytes from image base */
    uint64_t flags;                  /* HXE_SEG_* */
};

/* Load an HXE1 image into `space`, mapping/zeroing/copying every segment, and
 * write the entry point to *out_entry. Returns 0 on success, negative on a
 * malformed image. Does not start the program. */
int user_loader_load(struct user_address_space *space, const void *image,
                     uint64_t image_size, uint64_t *out_entry);

#endif /* MYOS_USER_LOADER_H */
