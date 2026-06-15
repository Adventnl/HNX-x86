/* Safe integer arithmetic and small math helpers for the kernel.
 *
 * Overflow-checked add/mul return 0 on success and -1 (leaving *out untouched)
 * on overflow, so callers validating untrusted sizes (syscall args, on-disk
 * lengths, descriptor counts) can refuse bad input instead of wrapping.
 */
#ifndef MYOS_LIB_KMATH_H
#define MYOS_LIB_KMATH_H

#include "types.h"

static inline int check_add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (a > (uint64_t)~0ULL - b) {
        return -1;
    }
    *out = a + b;
    return 0;
}

static inline int check_mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (a != 0 && b > (uint64_t)~0ULL / a) {
        return -1;
    }
    *out = a * b;
    return 0;
}

static inline int check_add_size(size_t a, size_t b, size_t *out) {
    if (a > (size_t)~(size_t)0 - b) {
        return -1;
    }
    *out = a + b;
    return 0;
}

static inline int check_mul_size(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > (size_t)~(size_t)0 / a) {
        return -1;
    }
    *out = a * b;
    return 0;
}

static inline uint64_t align_up_u64(uint64_t x, uint64_t a) {
    return (x + (a - 1)) & ~(a - 1);
}
static inline uint64_t align_down_u64(uint64_t x, uint64_t a) {
    return x & ~(a - 1);
}
static inline int is_pow2_u64(uint64_t x) {
    return x != 0 && (x & (x - 1)) == 0;
}

/* Integer log2 floor (0 for x==0). */
static inline unsigned ilog2_u64(uint64_t x) {
    unsigned r = 0;
    while (x >>= 1) {
        r++;
    }
    return r;
}

/* ceil(a / b) without overflow for b != 0. */
static inline uint64_t div_round_up_u64(uint64_t a, uint64_t b) {
    return b ? (a + b - 1) / b : 0;
}

static inline uint64_t min_u64(uint64_t a, uint64_t b) { return a < b ? a : b; }
static inline uint64_t max_u64(uint64_t a, uint64_t b) { return a > b ? a : b; }
static inline int64_t  clamp_i64(int64_t v, int64_t lo, int64_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

#endif /* MYOS_LIB_KMATH_H */
