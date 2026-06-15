/* ID allocator implementation (see kernel/lib/idr.h). */
#include "idr.h"
#include "heap.h"

int idr_init(struct idr *idr, uint32_t base, uint32_t capacity) {
    size_t words = BITMAP_WORDS(capacity);
    idr->storage = (uint64_t *)kcalloc(words, sizeof(uint64_t));
    if (!idr->storage) {
        return -1;
    }
    bitmap_attach(&idr->map, idr->storage, capacity);
    bitmap_zero(&idr->map);
    idr->base = base;
    idr->capacity = capacity;
    idr->next_hint = 0;
    return 0;
}

void idr_destroy(struct idr *idr) {
    if (idr->storage) {
        kfree(idr->storage);
        idr->storage = NULL;
    }
    idr->capacity = 0;
}

int idr_alloc(struct idr *idr) {
    /* Search from the round-robin hint, then wrap to the start. */
    for (uint32_t pass = 0; pass < 2; pass++) {
        uint32_t start = pass == 0 ? idr->next_hint : 0;
        uint32_t end = pass == 0 ? idr->capacity : idr->next_hint;
        for (uint32_t i = start; i < end; i++) {
            if (!bitmap_test(&idr->map, i)) {
                bitmap_set(&idr->map, i);
                idr->next_hint = (i + 1 < idr->capacity) ? i + 1 : 0;
                return (int)(idr->base + i);
            }
        }
    }
    return -1;
}

int idr_reserve(struct idr *idr, uint32_t id) {
    if (id < idr->base) {
        return -1;
    }
    uint32_t i = id - idr->base;
    if (i >= idr->capacity || bitmap_test(&idr->map, i)) {
        return -1;
    }
    bitmap_set(&idr->map, i);
    return 0;
}

void idr_free(struct idr *idr, uint32_t id) {
    if (id < idr->base) {
        return;
    }
    uint32_t i = id - idr->base;
    if (i < idr->capacity) {
        bitmap_clear(&idr->map, i);
    }
}

int idr_in_use(const struct idr *idr, uint32_t id) {
    if (id < idr->base) {
        return 0;
    }
    uint32_t i = id - idr->base;
    if (i >= idr->capacity) {
        return 0;
    }
    return bitmap_test(&idr->map, i);
}

uint32_t idr_used(const struct idr *idr) {
    return (uint32_t)bitmap_weight(&idr->map);
}
