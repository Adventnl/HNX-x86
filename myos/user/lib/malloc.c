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

void *malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    uint64_t start = align_up(g_cursor, ALIGN);
    uint64_t end = start + size;
    if (end > USER_HEAP_BASE + USER_HEAP_MAPPED) {
        return NULL;   /* out of mapped heap */
    }
    g_cursor = end;
    return (void *)(uintptr_t)start;
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
