/* Kernel heap: early bump allocator backed by a static .bss arena.
 *
 * For Prompt 2 this is intentionally simple. kfree() is a no-op: freed memory
 * is not reclaimed by the bump allocator. A real free-list / slab allocator is
 * a later milestone. The arena lives in .bss so it needs no VMM mapping (it is
 * part of the kernel image, which is identity-mapped and higher-half mapped).
 */
#include "heap.h"
#include "log.h"
#include "panic.h"

#define HEAP_ARENA_SIZE (2 * 1024 * 1024)   /* 2 MiB */
#define HEAP_ALIGN      16

static uint8_t g_arena[HEAP_ARENA_SIZE] __attribute__((aligned(HEAP_ALIGN)));
static uint64_t g_offset;
static uint64_t g_alloc_count;

void heap_init(void) {
    g_offset = 0;
    g_alloc_count = 0;
}

static uint64_t align_up(uint64_t x, uint64_t a) {
    return (x + (a - 1)) & ~(a - 1);
}

void *kmalloc(uint64_t size) {
    if (size == 0) {
        return NULL;
    }
    uint64_t start = align_up(g_offset, HEAP_ALIGN);
    if (start + size > HEAP_ARENA_SIZE) {
        kernel_panic_hex("heap: out of memory, requested", size);
    }
    g_offset = start + size;
    g_alloc_count++;
    return &g_arena[start];
}

void *kcalloc(uint64_t count, uint64_t size) {
    uint64_t total = count * size;
    void *p = kmalloc(total);
    if (p) {
        uint8_t *b = (uint8_t *)p;
        for (uint64_t i = 0; i < total; i++) {
            b[i] = 0;
        }
    }
    return p;
}

void kfree(void *ptr) {
    /* No-op: the bump allocator does not reclaim memory (documented). */
    (void)ptr;
}

void heap_dump_stats(void) {
    kernel_log_hex64("    heap size   : ", HEAP_ARENA_SIZE);
    kernel_log_hex64("    heap used   : ", g_offset);
    kernel_log_hex64("    allocations : ", g_alloc_count);
}
