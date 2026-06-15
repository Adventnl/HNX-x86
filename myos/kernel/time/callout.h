/* Timer callouts: one-shot and periodic timers driven by the kernel tick.
 *
 * Two cooperating structures:
 *   - a sorted list of all armed callouts (authoritative, exact),
 *   - a hashed timer wheel that buckets near-future expiries for O(1) amortized
 *     advance without scanning every armed timer each tick.
 *
 * callout_advance(now) fires everything due at or before `now`. Periodic
 * callouts re-arm themselves. A virtual `now` can be supplied by tests; the
 * real driver passes the global tick.
 */
#ifndef MYOS_TIME_CALLOUT_H
#define MYOS_TIME_CALLOUT_H

#include "types.h"
#include "list.h"

#define CALLOUT_WHEEL_BITS 8
#define CALLOUT_WHEEL_SIZE (1u << CALLOUT_WHEEL_BITS)
#define CALLOUT_WHEEL_MASK (CALLOUT_WHEEL_SIZE - 1u)

struct callout {
    struct list_node sorted_link;  /* position in the global sorted list */
    struct list_node wheel_link;   /* position in its wheel bucket */
    uint64_t  expires;             /* absolute tick of next fire */
    uint64_t  period;              /* 0 = one-shot, else re-arm interval */
    void    (*fn)(struct callout *, void *);
    void     *arg;
    int       armed;
    uint64_t  fire_count;
};

struct callout_base {
    struct list_node sorted;                     /* ascending by expires */
    struct list_node wheel[CALLOUT_WHEEL_SIZE];
    uint64_t now;
    size_t   armed_count;
    uint64_t total_fired;
};

void callout_base_init(struct callout_base *b);
void callout_init(struct callout *c, void (*fn)(struct callout *, void *), void *arg);

/* Arm one-shot to fire `delay` ticks from base->now. */
void callout_arm(struct callout_base *b, struct callout *c, uint64_t delay);
/* Arm periodic: first fire after `delay`, then every `period` ticks. */
void callout_arm_periodic(struct callout_base *b, struct callout *c,
                          uint64_t delay, uint64_t period);
/* Cancel; returns 1 if it was armed. */
int  callout_cancel(struct callout_base *b, struct callout *c);

/* Advance the clock to `now`, firing all due callouts in expiry order.
 * Returns the number fired. */
int  callout_advance(struct callout_base *b, uint64_t now);

uint64_t callout_next_expiry(struct callout_base *b); /* 0 if none armed */
size_t   callout_armed(struct callout_base *b);

#endif /* MYOS_TIME_CALLOUT_H */
