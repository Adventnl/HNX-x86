/* Fixed-storage bitmap library.
 *
 * Operates on a caller-provided array of 64-bit words. All indices are bit
 * positions; helpers convert to word/offset internally. Used by the PMM-style
 * allocators, the slab free maps, and the id allocator.
 */
#ifndef MYOS_LIB_BITMAP_H
#define MYOS_LIB_BITMAP_H

#include "types.h"

#define BITMAP_WORD_BITS 64u
#define BITMAP_WORDS(nbits) (((nbits) + BITMAP_WORD_BITS - 1) / BITMAP_WORD_BITS)
#define BITMAP_BYTES(nbits) (BITMAP_WORDS(nbits) * sizeof(uint64_t))

struct bitmap {
    uint64_t *words;
    size_t    nbits;
};

static inline void bitmap_attach(struct bitmap *b, uint64_t *words, size_t nbits) {
    b->words = words;
    b->nbits = nbits;
}

void bitmap_zero(struct bitmap *b);
void bitmap_fill(struct bitmap *b);

void bitmap_set(struct bitmap *b, size_t bit);
void bitmap_clear(struct bitmap *b, size_t bit);
int  bitmap_test(const struct bitmap *b, size_t bit);

/* Set/clear a contiguous range [start, start+count). */
void bitmap_set_range(struct bitmap *b, size_t start, size_t count);
void bitmap_clear_range(struct bitmap *b, size_t start, size_t count);

/* Population count over the whole bitmap. */
size_t bitmap_weight(const struct bitmap *b);

/* First bit that is zero / set, or (size_t)-1 if none. */
size_t bitmap_find_first_zero(const struct bitmap *b);
size_t bitmap_find_first_set(const struct bitmap *b);

/* First run of `count` consecutive zero bits, or (size_t)-1. Does not set. */
size_t bitmap_find_zero_run(const struct bitmap *b, size_t count);

/* Atomically (for the single-CPU kernel: simply) find a zero bit and set it.
 * Returns the bit index or (size_t)-1 when full. */
size_t bitmap_alloc(struct bitmap *b);

#define BITMAP_NONE ((size_t)-1)

#endif /* MYOS_LIB_BITMAP_H */
