/* Lightweight kernel tracing framework.
 *
 * Records (timestamp, category, id, message, two scalar args) events into a
 * fixed ring. Categories can be enabled/disabled at runtime via a mask so a
 * subsystem can be traced without recompiling. Designed to be cheap enough to
 * leave compiled in: a disabled category costs one mask test.
 */
#ifndef MYOS_DEBUG_KTRACE_H
#define MYOS_DEBUG_KTRACE_H

#include "types.h"

enum ktrace_cat {
    KTRACE_CAT_SCHED = 0,
    KTRACE_CAT_MEM,
    KTRACE_CAT_IRQ,
    KTRACE_CAT_FS,
    KTRACE_CAT_BLOCK,
    KTRACE_CAT_NET,
    KTRACE_CAT_USB,
    KTRACE_CAT_SYSCALL,
    KTRACE_CAT_DRIVER,
    KTRACE_CAT_MAX
};

#define KTRACE_RING_EVENTS 1024

struct ktrace_event {
    uint64_t        seq;
    uint64_t        tick;
    enum ktrace_cat cat;
    uint32_t        id;
    uint64_t        arg0;
    uint64_t        arg1;
    const char     *msg;
};

void ktrace_init(void);
void ktrace_enable(enum ktrace_cat cat);
void ktrace_disable(enum ktrace_cat cat);
void ktrace_enable_all(void);
void ktrace_disable_all(void);
int  ktrace_enabled(enum ktrace_cat cat);

void ktrace_emit(enum ktrace_cat cat, uint32_t id, const char *msg,
                 uint64_t arg0, uint64_t arg1);

#define KTRACE(cat, id, msg, a0, a1)                       \
    do {                                                   \
        if (ktrace_enabled(cat)) {                         \
            ktrace_emit((cat), (id), (msg), (a0), (a1));   \
        }                                                  \
    } while (0)

uint64_t ktrace_count(void);                 /* lifetime events */
size_t   ktrace_snapshot(struct ktrace_event *out, size_t max);  /* recent first */
const char *ktrace_cat_name(enum ktrace_cat cat);
void     ktrace_dump(size_t max);

#endif /* MYOS_DEBUG_KTRACE_H */
