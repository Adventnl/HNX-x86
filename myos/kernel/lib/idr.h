/* ID allocator (small integer handle allocator) backed by a bitmap.
 *
 * Hands out the lowest free id in [base, base+capacity). Used for pids, fds,
 * device minor numbers and handle slots. O(words) allocation; O(1) free.
 */
#ifndef MYOS_LIB_IDR_H
#define MYOS_LIB_IDR_H

#include "types.h"
#include "bitmap.h"

struct idr {
    struct bitmap map;
    uint64_t     *storage;   /* kmalloc'd bitmap words */
    uint32_t      base;
    uint32_t      capacity;
    uint32_t      next_hint; /* round-robin start to reduce id reuse churn */
};

/* Returns 0 on success, -1 on allocation failure. */
int  idr_init(struct idr *idr, uint32_t base, uint32_t capacity);
void idr_destroy(struct idr *idr);

/* Allocate the next free id, or -1 when exhausted. */
int  idr_alloc(struct idr *idr);
/* Reserve a specific id; returns 0 on success, -1 if out of range/taken. */
int  idr_reserve(struct idr *idr, uint32_t id);
/* Release an id. */
void idr_free(struct idr *idr, uint32_t id);
int  idr_in_use(const struct idr *idr, uint32_t id);
uint32_t idr_used(const struct idr *idr);

#endif /* MYOS_LIB_IDR_H */
