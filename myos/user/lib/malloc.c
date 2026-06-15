/* User bump allocator over the kernel-mapped user heap region.
 *
 * The kernel maps USER_HEAP_INITIAL bytes at USER_HEAP_BASE when the process is
 * created (see kernel/user/user.h). This allocator hands out 16-byte-aligned
 * chunks from that window; free() is a no-op (a real free-list is a later
 * milestone). Allocations beyond the mapped window return NULL. */
#include "stdlib.h"
#include "string.h"

#define USER_HEAP_BASE   0x0000004000000000ULL
#define USER_HEAP_MAPPED 0x0000000000100000ULL   /* matches USER_HEAP_INITIAL */
#define ALIGN            16ULL

static uint64_t g_cursor = USER_HEAP_BASE;

static uint64_t align_up(uint64_t x, uint64_t a) {
    return (x + (a - 1)) & ~(a - 1);
}

/* Each allocation is preceded by an ALIGN-byte header storing its payload size
 * so realloc() knows how many bytes to copy. */
void *malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    uint64_t hdr = align_up(g_cursor, ALIGN);
    uint64_t start = hdr + ALIGN;       /* payload begins after the header */
    uint64_t end = start + size;
    if (end > USER_HEAP_BASE + USER_HEAP_MAPPED) {
        return NULL;   /* out of mapped heap */
    }
    *(uint64_t *)(uintptr_t)hdr = (uint64_t)size;
    g_cursor = end;
    return (void *)(uintptr_t)start;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    uint64_t old_size = *(uint64_t *)((uintptr_t)ptr - ALIGN);
    void *np = malloc(size);
    if (!np) {
        return NULL;
    }
    size_t copy = (old_size < size) ? (size_t)old_size : size;
    memcpy(np, ptr, copy);
    return np;
}

void *calloc(size_t count, size_t size) {
    size_t total = count * size;
    void *p = malloc(total);
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

void free(void *ptr) {
    (void)ptr;   /* no-op: bump allocator does not reclaim */
}
