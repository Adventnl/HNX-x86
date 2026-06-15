/* Timer callout implementation (see kernel/time/callout.h). */
#include "callout.h"

void callout_base_init(struct callout_base *b) {
    list_init(&b->sorted);
    for (unsigned i = 0; i < CALLOUT_WHEEL_SIZE; i++) {
        list_init(&b->wheel[i]);
    }
    b->now = 0;
    b->armed_count = 0;
    b->total_fired = 0;
}

void callout_init(struct callout *c, void (*fn)(struct callout *, void *), void *arg) {
    list_init(&c->sorted_link);
    list_init(&c->wheel_link);
    c->expires = 0;
    c->period = 0;
    c->fn = fn;
    c->arg = arg;
    c->armed = 0;
    c->fire_count = 0;
}

static void insert_sorted(struct callout_base *b, struct callout *c) {
    /* Keep the sorted list in ascending expiry order. */
    struct list_node *pos = &b->sorted;
    struct list_node *p;
    list_for_each(p, &b->sorted) {
        struct callout *oc = list_entry(p, struct callout, sorted_link);
        if (oc->expires > c->expires) {
            pos = p;
            break;
        }
    }
    if (pos == &b->sorted) {
        list_add_tail(&c->sorted_link, &b->sorted);
    } else {
        __list_add(&c->sorted_link, pos->prev, pos);
    }
    unsigned bucket = (unsigned)(c->expires & CALLOUT_WHEEL_MASK);
    list_add_tail(&c->wheel_link, &b->wheel[bucket]);
}

static void unlink(struct callout *c) {
    if (list_linked(&c->sorted_link)) {
        list_del_init(&c->sorted_link);
    }
    if (list_linked(&c->wheel_link)) {
        list_del_init(&c->wheel_link);
    }
}

void callout_arm(struct callout_base *b, struct callout *c, uint64_t delay) {
    callout_arm_periodic(b, c, delay, 0);
}

void callout_arm_periodic(struct callout_base *b, struct callout *c,
                          uint64_t delay, uint64_t period) {
    if (c->armed) {
        unlink(c);
        if (b->armed_count) {
            b->armed_count--;
        }
    }
    c->expires = b->now + delay;
    c->period = period;
    c->armed = 1;
    insert_sorted(b, c);
    b->armed_count++;
}

int callout_cancel(struct callout_base *b, struct callout *c) {
    if (!c->armed) {
        return 0;
    }
    unlink(c);
    c->armed = 0;
    if (b->armed_count) {
        b->armed_count--;
    }
    return 1;
}

int callout_advance(struct callout_base *b, uint64_t now) {
    int fired = 0;
    b->now = now;
    for (;;) {
        if (list_empty(&b->sorted)) {
            break;
        }
        struct callout *c = list_first_entry(&b->sorted, struct callout, sorted_link);
        if (c->expires > now) {
            break;
        }
        /* Due: detach, fire, and re-arm if periodic. */
        unlink(c);
        c->armed = 0;
        if (b->armed_count) {
            b->armed_count--;
        }
        c->fire_count++;
        b->total_fired++;
        fired++;
        if (c->fn) {
            c->fn(c, c->arg);
        }
        if (c->period) {
            /* Re-arm relative to its scheduled expiry to avoid drift. The outer
             * loop will fire it again in this same advance if it is still due,
             * so a large jump produces one firing per elapsed period. */
            c->expires += c->period;
            c->armed = 1;
            insert_sorted(b, c);
            b->armed_count++;
        }
    }
    return fired;
}

uint64_t callout_next_expiry(struct callout_base *b) {
    if (list_empty(&b->sorted)) {
        return 0;
    }
    struct callout *c = list_first_entry(&b->sorted, struct callout, sorted_link);
    return c->expires;
}

size_t callout_armed(struct callout_base *b) {
    return b->armed_count;
}
