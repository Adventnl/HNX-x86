/* Kernel heap: a real first-fit free-list allocator over a static .bss arena.
 *
 * Production Overhaul Phase 1 replaced the original bump allocator (whose kfree
 * was a no-op) with an implicit free-list allocator that actually reclaims
 * memory: kfree() marks a block free and coalesces it with its neighbours, so a
 * long-running boot that spawns dozens of processes no longer exhausts the
 * arena. The arena lives in .bss (identity- and higher-half mapped with the
 * kernel image), so it needs no VMM mapping. The slab allocator
 * (kernel/memory/slab.c) sits on the page allocator for object caches; this heap
 * remains the general kmalloc/kfree used across the kernel.
 *
 * Block layout (each block is 16-byte aligned):
 *   [ struct block_header | payload ... ]
 * Blocks tile the arena contiguously; size is the whole block (header+payload),
 * so the next block is at (uint8_t*)blk + blk->size. Coalescing walks the arena
 * merging adjacent free blocks.
 */
#include "heap.h"
#include "log.h"
#include "panic.h"

/* 2 MiB static .bss arena (the bootloader places the kernel image at a fixed
 * low address, so the arena cannot grow much without colliding with other
 * UEFI allocations). With the reclaiming free list below this is ample: peak
 * simultaneous usage is bounded, not cumulative. */
#define HEAP_ARENA_SIZE (2 * 1024 * 1024)   /* 2 MiB */
#define HEAP_ALIGN      16u
#define HEAP_MAGIC      0x484E5848u          /* 'HNXH' */

struct block_header {
    uint32_t size;      /* total block size (header + payload), 16-aligned */
    uint32_t free;      /* 1 = free, 0 = allocated */
    uint32_t magic;     /* HEAP_MAGIC when allocated (double-free guard) */
    uint32_t _pad;      /* keep header 16 bytes so payload is 16-aligned */
};

static uint8_t g_arena[HEAP_ARENA_SIZE] __attribute__((aligned(HEAP_ALIGN)));
static uint64_t g_alloc_count;
static uint64_t g_free_count;
static uint64_t g_bytes_in_use;
static uint64_t g_high_water;

static inline uint64_t align_up(uint64_t x, uint64_t a) {
    return (x + (a - 1)) & ~(a - 1);
}

void heap_init(void) {
    g_alloc_count = 0;
    g_free_count = 0;
    g_bytes_in_use = 0;
    g_high_water = 0;
    /* One big free block spanning the whole arena. */
    struct block_header *first = (struct block_header *)g_arena;
    first->size = HEAP_ARENA_SIZE;
    first->free = 1;
    first->magic = 0;
    first->_pad = 0;
}

static struct block_header *next_block(struct block_header *b) {
    uint8_t *p = (uint8_t *)b + b->size;
    if (p >= g_arena + HEAP_ARENA_SIZE) {
        return NULL;
    }
    return (struct block_header *)p;
}

/* Merge every run of adjacent free blocks. O(blocks); called on free. */
static void coalesce(void) {
    struct block_header *b = (struct block_header *)g_arena;
    while (b) {
        if (b->free) {
            struct block_header *n = next_block(b);
            while (n && n->free) {
                b->size += n->size;        /* absorb the neighbour */
                n = next_block(b);
            }
        }
        b = next_block(b);
    }
}

void *kmalloc(uint64_t size) {
    if (size == 0) {
        return NULL;
    }
    uint64_t need = align_up(sizeof(struct block_header) + size, HEAP_ALIGN);

    struct block_header *b = (struct block_header *)g_arena;
    while (b) {
        if (b->free && b->size >= need) {
            /* Split if the leftover can hold a header + a minimal payload. */
            uint64_t leftover = b->size - need;
            if (leftover >= sizeof(struct block_header) + HEAP_ALIGN) {
                struct block_header *split =
                    (struct block_header *)((uint8_t *)b + need);
                split->size = (uint32_t)leftover;
                split->free = 1;
                split->magic = 0;
                split->_pad = 0;
                b->size = (uint32_t)need;
            }
            b->free = 0;
            b->magic = HEAP_MAGIC;
            g_alloc_count++;
            g_bytes_in_use += b->size;
            if (g_bytes_in_use > g_high_water) {
                g_high_water = g_bytes_in_use;
            }
            return (void *)((uint8_t *)b + sizeof(struct block_header));
        }
        b = next_block(b);
    }
    kernel_panic_hex("heap: out of memory, requested", size);
}

void *kcalloc(uint64_t count, uint64_t size) {
    uint64_t total = count * size;
    /* Overflow guard. */
    if (count != 0 && total / count != size) {
        return NULL;
    }
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
    if (ptr == NULL) {
        return;
    }
    /* Reject pointers that are not from this arena. */
    if ((uint8_t *)ptr < g_arena + sizeof(struct block_header) ||
        (uint8_t *)ptr >= g_arena + HEAP_ARENA_SIZE) {
        return;
    }
    struct block_header *b =
        (struct block_header *)((uint8_t *)ptr - sizeof(struct block_header));
    if (b->magic != HEAP_MAGIC) {
        /* Double free or corruption: ignore rather than corrupt the arena. */
        kernel_log_error("kfree: bad/double free ignored");
        return;
    }
    b->free = 1;
    b->magic = 0;
    if (g_bytes_in_use >= b->size) {
        g_bytes_in_use -= b->size;
    }
    g_free_count++;
    coalesce();
}

void heap_dump_stats(void) {
    /* Count free bytes + largest free run for diagnostics. */
    uint64_t free_bytes = 0;
    uint64_t largest_free = 0;
    struct block_header *b = (struct block_header *)g_arena;
    while (b) {
        if (b->free) {
            free_bytes += b->size;
            if (b->size > largest_free) {
                largest_free = b->size;
            }
        }
        b = next_block(b);
    }
    kernel_log_hex64("    heap size   : ", HEAP_ARENA_SIZE);
    kernel_log_hex64("    heap in use : ", g_bytes_in_use);
    kernel_log_hex64("    heap free   : ", free_bytes);
    kernel_log_hex64("    largest free: ", largest_free);
    kernel_log_hex64("    allocations : ", g_alloc_count);
    kernel_log_hex64("    frees       : ", g_free_count);
    kernel_log_hex64("    high water  : ", g_high_water);
}
