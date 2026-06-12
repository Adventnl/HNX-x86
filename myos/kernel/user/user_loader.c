/* HXE1 loader: validate the header/segment table, then map + populate each
 * PT_LOAD-derived segment into the target user address space. */
#include "user_loader.h"
#include "user.h"
#include "user_address_space.h"
#include "paging.h"
#include "memory_layout.h"
#include "log.h"

static int seg_executable(const struct hxe_segment *s) {
    return (s->flags & HXE_SEG_EXEC) != 0;
}

int user_loader_load(struct user_address_space *space, const void *image,
                     uint64_t image_size, uint64_t *out_entry) {
    if (!space || !image || image_size < sizeof(struct hxe_header)) {
        return -1;
    }
    const uint8_t *base = (const uint8_t *)image;
    const struct hxe_header *h = (const struct hxe_header *)image;

    if (h->magic != HXE_MAGIC || h->version != HXE_VERSION) {
        return -1;
    }
    if (h->header_size < sizeof(struct hxe_header)) {
        return -1;
    }
    uint64_t table_off = h->header_size;
    uint64_t table_bytes = h->segment_count * sizeof(struct hxe_segment);
    if (h->segment_count == 0 || table_off + table_bytes > image_size) {
        return -1;
    }
    const struct hxe_segment *segs = (const struct hxe_segment *)(base + table_off);

    /* Validate every segment before mapping anything. */
    for (uint64_t i = 0; i < h->segment_count; i++) {
        const struct hxe_segment *s = &segs[i];
        if (s->file_size > s->memory_size) {
            return -1;
        }
        if (s->virtual_address >= USER_TOP ||
            s->virtual_address + s->memory_size > USER_TOP ||
            s->virtual_address + s->memory_size < s->virtual_address) {
            return -1;                          /* outside user range / overflow */
        }
        if (s->virtual_address < USER_IMAGE_BASE) {
            return -1;                          /* below the image base */
        }
        if (s->file_size &&
            (s->file_offset + s->file_size > image_size ||
             s->file_offset + s->file_size < s->file_offset)) {
            return -1;                          /* file bytes out of image */
        }
        /* No overlap with earlier segments (page-granular). */
        uint64_t a0 = PAGE_ALIGN_DOWN(s->virtual_address);
        uint64_t a1 = PAGE_ALIGN_UP(s->virtual_address + s->memory_size);
        for (uint64_t j = 0; j < i; j++) {
            uint64_t b0 = PAGE_ALIGN_DOWN(segs[j].virtual_address);
            uint64_t b1 = PAGE_ALIGN_UP(segs[j].virtual_address + segs[j].memory_size);
            if (a0 < b1 && b0 < a1) {
                return -1;                      /* overlapping segments */
            }
        }
    }

    /* The entry must fall inside an executable segment. */
    int entry_ok = 0;
    for (uint64_t i = 0; i < h->segment_count; i++) {
        const struct hxe_segment *s = &segs[i];
        if (seg_executable(s) && h->entry >= s->virtual_address &&
            h->entry < s->virtual_address + s->memory_size) {
            entry_ok = 1;
            break;
        }
    }
    if (!entry_ok) {
        return -1;
    }

    /* Map (page-aligned), zero, then copy file bytes for each segment. */
    for (uint64_t i = 0; i < h->segment_count; i++) {
        const struct hxe_segment *s = &segs[i];
        uint64_t page_va = PAGE_ALIGN_DOWN(s->virtual_address);
        uint64_t span = PAGE_ALIGN_UP(s->virtual_address + s->memory_size) - page_va;
        uint64_t flags = (s->flags & HXE_SEG_WRITE) ? PAGE_WRITABLE : 0;
        if (user_map_range(space, page_va, span, flags) != 0) {
            return -1;                          /* map_range zero-fills (bss) */
        }
        if (s->file_size) {
            if (user_copy_to_space(space, s->virtual_address,
                                   (const uint8_t *)image + s->file_offset,
                                   s->file_size) != 0) {
                return -1;
            }
        }
    }

    if (out_entry) {
        *out_entry = h->entry;
    }
    return 0;
}
