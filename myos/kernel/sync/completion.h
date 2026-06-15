/* Completion: a one-shot (or re-armable) "event happened" signal.
 *
 * A thread waits for completion; another thread signals it. Used for "driver
 * finished bringing up the device", "DMA transfer done", "child reached a
 * checkpoint". If the event already fired before the waiter arrives, the wait
 * returns immediately (the done flag is sticky until reset).
 */
#ifndef MYOS_SYNC_COMPLETION_H
#define MYOS_SYNC_COMPLETION_H

#include "types.h"
#include "spinlock.h"
#include "waitqueue.h"

struct completion {
    int               done;
    struct spinlock   lock;
    struct waitqueue  waiters;
    const char       *name;
};

void completion_init(struct completion *c, const char *name);
void completion_reset(struct completion *c);

void wait_for_completion(struct completion *c);
int  try_wait_for_completion(struct completion *c);   /* 1 if done, else 0 */

void complete(struct completion *c);       /* wake one waiter */
void complete_all(struct completion *c);   /* wake every waiter */

int  completion_done(const struct completion *c);

#endif /* MYOS_SYNC_COMPLETION_H */
