/* Bitmap library implementation (see kernel/lib/bitmap.h). */
#include "bitmap.h"

static inline size_t word_of(size_t bit) { return bit / BITMAP_WORD_BITS; }
static inline unsigned off_of(size_t bit) { return (unsigned)(bit % BITMAP_WORD_BITS); }

void bitmap_zero(struct bitmap *b) {
    size_t words = BITMAP_WORDS(b->nbits);
    for (size_t i = 0; i < words; i++) {
        b->words[i] = 0;
    }
}

void bitmap_fill(struct bitmap *b) {
    size_t words = BITMAP_WORDS(b->nbits);
    for (size_t i = 0; i < words; i++) {
        b->words[i] = ~0ULL;
    }
    /* Clear the padding bits in the final word so weight() stays accurate. */
    unsigned tail = (unsigned)(b->nbits % BITMAP_WORD_BITS);
    if (tail) {
        b->words[words - 1] &= (1ULL << tail) - 1;
    }
}

void bitmap_set(struct bitmap *b, size_t bit) {
    if (bit >= b->nbits) {
        return;
    }
    b->words[word_of(bit)] |= 1ULL << off_of(bit);
}

void bitmap_clear(struct bitmap *b, size_t bit) {
    if (bit >= b->nbits) {
        return;
    }
    b->words[word_of(bit)] &= ~(1ULL << off_of(bit));
}

int bitmap_test(const struct bitmap *b, size_t bit) {
    if (bit >= b->nbits) {
        return 0;
    }
    return (b->words[word_of(bit)] >> off_of(bit)) & 1ULL;
}

void bitmap_set_range(struct bitmap *b, size_t start, size_t count) {
    for (size_t i = 0; i < count; i++) {
        bitmap_set(b, start + i);
    }
}

void bitmap_clear_range(struct bitmap *b, size_t start, size_t count) {
    for (size_t i = 0; i < count; i++) {
        bitmap_clear(b, start + i);
    }
}

static unsigned popcount64(uint64_t v) {
    /* SWAR popcount; no hardware popcnt dependency. */
    v = v - ((v >> 1) & 0x5555555555555555ULL);
    v = (v & 0x3333333333333333ULL) + ((v >> 2) & 0x3333333333333333ULL);
    v = (v + (v >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (unsigned)((v * 0x0101010101010101ULL) >> 56);
}

size_t bitmap_weight(const struct bitmap *b) {
    size_t words = BITMAP_WORDS(b->nbits);
    size_t total = 0;
    for (size_t i = 0; i < words; i++) {
        total += popcount64(b->words[i]);
    }
    return total;
}

static unsigned ctz64(uint64_t v) {
    if (v == 0) {
        return 64;
    }
    unsigned n = 0;
    while ((v & 1ULL) == 0) {
        v >>= 1;
        n++;
    }
    return n;
}

size_t bitmap_find_first_zero(const struct bitmap *b) {
    size_t words = BITMAP_WORDS(b->nbits);
    for (size_t i = 0; i < words; i++) {
        uint64_t inv = ~b->words[i];
        if (inv) {
            size_t bit = i * BITMAP_WORD_BITS + ctz64(inv);
            if (bit < b->nbits) {
                return bit;
            }
            return BITMAP_NONE;
        }
    }
    return BITMAP_NONE;
}

size_t bitmap_find_first_set(const struct bitmap *b) {
    size_t words = BITMAP_WORDS(b->nbits);
    for (size_t i = 0; i < words; i++) {
        if (b->words[i]) {
            size_t bit = i * BITMAP_WORD_BITS + ctz64(b->words[i]);
            if (bit < b->nbits) {
                return bit;
            }
            return BITMAP_NONE;
        }
    }
    return BITMAP_NONE;
}

size_t bitmap_find_zero_run(const struct bitmap *b, size_t count) {
    if (count == 0) {
        return BITMAP_NONE;
    }
    size_t run = 0;
    size_t start = 0;
    for (size_t bit = 0; bit < b->nbits; bit++) {
        if (!bitmap_test(b, bit)) {
            if (run == 0) {
                start = bit;
            }
            if (++run == count) {
                return start;
            }
        } else {
            run = 0;
        }
    }
    return BITMAP_NONE;
}

size_t bitmap_alloc(struct bitmap *b) {
    size_t bit = bitmap_find_first_zero(b);
    if (bit != BITMAP_NONE) {
        bitmap_set(b, bit);
    }
    return bit;
}
