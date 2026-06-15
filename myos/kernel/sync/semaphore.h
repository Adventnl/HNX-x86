/* Counting semaphore. down() blocks while the count is zero; up() releases a
 * unit and wakes a waiter. A binary semaphore (max == 1) acts as a signal.
 */
#ifndef MYOS_SYNC_SEMAPHORE_H
#define MYOS_SYNC_SEMAPHORE_H

#include "types.h"
#include "spinlock.h"
#include "waitqueue.h"

struct semaphore {
    int               count;
    int               max;       /* informational ceiling (0 = unbounded) */
    struct spinlock   lock;
    struct waitqueue  waiters;
    const char       *name;
};

void semaphore_init(struct semaphore *s, int initial, int max, const char *name);

void semaphore_down(struct semaphore *s);        /* P / wait  */
int  semaphore_trydown(struct semaphore *s);     /* 1 if acquired, 0 if would block */
void semaphore_up(struct semaphore *s);          /* V / signal */
int  semaphore_count(const struct semaphore *s);

#endif /* MYOS_SYNC_SEMAPHORE_H */
