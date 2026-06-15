/* Atomic reference counting.
 *
 * A refcount starts at 1 (the creator's reference). get() must only be called
 * while holding an existing reference. put() drops a reference and returns 1
 * when the count reaches zero so the caller can release the object exactly once.
 */
#ifndef MYOS_OBJECT_REFCOUNT_H
#define MYOS_OBJECT_REFCOUNT_H

#include "types.h"
#include "atomic.h"

struct refcount {
    atomic_t count;
};

static inline void refcount_init(struct refcount *r, int32_t initial) {
    atomic_set(&r->count, initial);
}
static inline int32_t refcount_read(const struct refcount *r) {
    return atomic_read((atomic_t *)&r->count);
}
static inline void refcount_get(struct refcount *r) {
    atomic_inc(&r->count);
}
/* Returns 1 if this put dropped the last reference (count reached 0). */
static inline int refcount_put(struct refcount *r) {
    return atomic_dec(&r->count) == 0;
}
/* Take a reference only if the count is not already zero (weak->strong). */
static inline int refcount_get_unless_zero(struct refcount *r) {
    for (;;) {
        int32_t c = atomic_read(&r->count);
        if (c == 0) {
            return 0;
        }
        if (atomic_cas(&r->count, c, c + 1)) {
            return 1;
        }
    }
}

#endif /* MYOS_OBJECT_REFCOUNT_H */
