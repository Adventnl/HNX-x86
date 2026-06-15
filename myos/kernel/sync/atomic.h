/* Atomic integer operations.
 *
 * Thin wrappers over the compiler's __atomic builtins (sequentially consistent
 * unless noted). On the single-CPU kernel these compile to plain instructions
 * plus the necessary compiler barriers; the API is written so the code is
 * already correct should SMP arrive.
 */
#ifndef MYOS_SYNC_ATOMIC_H
#define MYOS_SYNC_ATOMIC_H

#include "types.h"

typedef struct { volatile int32_t v; }  atomic_t;
typedef struct { volatile int64_t v; }  atomic64_t;

#define ATOMIC_INIT(x)   { (x) }

static inline int32_t atomic_read(const atomic_t *a) {
    return __atomic_load_n(&a->v, __ATOMIC_SEQ_CST);
}
static inline void atomic_set(atomic_t *a, int32_t val) {
    __atomic_store_n(&a->v, val, __ATOMIC_SEQ_CST);
}
static inline int32_t atomic_add(atomic_t *a, int32_t delta) {
    return __atomic_add_fetch(&a->v, delta, __ATOMIC_SEQ_CST);
}
static inline int32_t atomic_sub(atomic_t *a, int32_t delta) {
    return __atomic_sub_fetch(&a->v, delta, __ATOMIC_SEQ_CST);
}
static inline int32_t atomic_inc(atomic_t *a) { return atomic_add(a, 1); }
static inline int32_t atomic_dec(atomic_t *a) { return atomic_sub(a, 1); }

/* Compare-and-swap: if *a == expected, store desired and return 1. */
static inline int atomic_cas(atomic_t *a, int32_t expected, int32_t desired) {
    return __atomic_compare_exchange_n(&a->v, &expected, desired, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
static inline int32_t atomic_xchg(atomic_t *a, int32_t val) {
    return __atomic_exchange_n(&a->v, val, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic64_read(const atomic64_t *a) {
    return __atomic_load_n(&a->v, __ATOMIC_SEQ_CST);
}
static inline void atomic64_set(atomic64_t *a, int64_t val) {
    __atomic_store_n(&a->v, val, __ATOMIC_SEQ_CST);
}
static inline int64_t atomic64_add(atomic64_t *a, int64_t delta) {
    return __atomic_add_fetch(&a->v, delta, __ATOMIC_SEQ_CST);
}
static inline int64_t atomic64_sub(atomic64_t *a, int64_t delta) {
    return __atomic_sub_fetch(&a->v, delta, __ATOMIC_SEQ_CST);
}
static inline int64_t atomic64_inc(atomic64_t *a) { return atomic64_add(a, 1); }
static inline int64_t atomic64_dec(atomic64_t *a) { return atomic64_sub(a, 1); }

static inline void smp_mb(void)  { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static inline void smp_rmb(void) { __atomic_thread_fence(__ATOMIC_ACQUIRE); }
static inline void smp_wmb(void) { __atomic_thread_fence(__ATOMIC_RELEASE); }
static inline void cpu_relax(void) { __asm__ volatile("pause" ::: "memory"); }

#endif /* MYOS_SYNC_ATOMIC_H */
