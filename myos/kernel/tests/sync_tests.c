/* Tests for kernel/sync: spinlocks, wait queues, mutex, rwlock, semaphore and
 * completion. Only the non-blocking / uncontended paths are exercised here so
 * the suite is deterministic in the single-threaded boot context; the blocking
 * paths are integration-tested by real threads later.
 * Markers: "wait queue tests", "mutex tests", "rwlock tests". */
#include "ktest.h"
#include "spinlock.h"
#include "waitqueue.h"
#include "mutex.h"
#include "rwlock.h"
#include "semaphore.h"
#include "completion.h"
#include "thread.h"

static void test_spinlock(void) {
    KT_BEGIN();
    struct spinlock l;
    spinlock_init(&l, "test");
    KT_CHECK(!spinlock_held(&l), "new lock free");
    spinlock_lock(&l);
    KT_CHECK(spinlock_held(&l), "held after lock");
    KT_CHECK(!spinlock_trylock(&l), "trylock fails when held");
    spinlock_unlock(&l);
    KT_CHECK(!spinlock_held(&l), "free after unlock");
    KT_CHECK(spinlock_trylock(&l), "trylock succeeds when free");
    spinlock_unlock(&l);

    /* IRQ-save path returns flags and restores them. */
    uint64_t f = spinlock_lock_irqsave(&l);
    KT_CHECK(spinlock_held(&l), "held under irqsave");
    spinlock_unlock_irqrestore(&l, f);
    KT_CHECK(!spinlock_held(&l), "free after irqrestore");
    KT_CHECK_EQ(l.acquisitions, 3, "acquisition count");
    KT_RESULT("spinlock tests");
}

static void test_waitqueue(void) {
    KT_BEGIN();
    struct waitqueue wq;
    waitqueue_init(&wq, "test");
    KT_CHECK(waitqueue_empty(&wq), "new wq empty");

    struct wait_entry e[3];
    for (int i = 0; i < 3; i++) {
        e[i].thread = NULL;     /* pure list test, no scheduler */
        waitqueue_enqueue(&wq, &e[i]);
    }
    KT_CHECK_EQ(waitqueue_len(&wq), 3, "three waiters");
    KT_CHECK(!waitqueue_empty(&wq), "wq not empty");

    /* Dequeue is FIFO. */
    struct wait_entry *first = waitqueue_dequeue(&wq);
    KT_CHECK_EQ((uintptr_t)first, (uintptr_t)&e[0], "FIFO dequeue");
    KT_CHECK_EQ(waitqueue_len(&wq), 2, "two left");

    /* wake_all empties and marks woken. */
    int woke = waitqueue_wake_all(&wq);
    KT_CHECK_EQ(woke, 2, "wake_all woke 2");
    KT_CHECK(e[1].woken && e[2].woken, "remaining entries marked woken");
    KT_CHECK(waitqueue_empty(&wq), "empty after wake_all");

    /* Remove a specific (re-enqueued) entry. */
    waitqueue_enqueue(&wq, &e[0]);
    waitqueue_remove(&wq, &e[0]);
    KT_CHECK(waitqueue_empty(&wq), "remove specific entry");
    KT_RESULT("wait queue tests");
}

static void test_mutex(void) {
    KT_BEGIN();
    struct mutex m;
    mutex_init(&m, "test");
    KT_CHECK(!mutex_is_locked(&m), "new mutex unlocked");

    mutex_lock(&m);
    KT_CHECK(mutex_is_locked(&m), "locked");
    KT_CHECK(mutex_owner(&m) == thread_current(), "owner is current");
    KT_CHECK(!mutex_trylock(&m), "trylock fails when held");
    mutex_unlock(&m);
    KT_CHECK(!mutex_is_locked(&m), "unlocked");
    KT_CHECK(mutex_owner(&m) == NULL, "owner cleared");

    KT_CHECK(mutex_trylock(&m), "trylock when free");
    mutex_unlock(&m);
    KT_CHECK_EQ(m.acquisitions, 2, "acquisition count");

    /* Semaphore acts as a bounded resource counter. */
    struct semaphore s;
    semaphore_init(&s, 2, 2, "test");
    KT_CHECK(semaphore_trydown(&s), "sem down 1");
    KT_CHECK(semaphore_trydown(&s), "sem down 2");
    KT_CHECK(!semaphore_trydown(&s), "sem exhausted");
    semaphore_up(&s);
    KT_CHECK_EQ(semaphore_count(&s), 1, "sem count restored");
    KT_CHECK(semaphore_trydown(&s), "sem reacquire");
    semaphore_up(&s);
    semaphore_up(&s);
    KT_CHECK_EQ(semaphore_count(&s), 2, "sem capped at max");

    /* Completion: already-done wait returns immediately. */
    struct completion c;
    completion_init(&c, "test");
    KT_CHECK(!try_wait_for_completion(&c), "not done yet");
    complete(&c);
    KT_CHECK(try_wait_for_completion(&c), "done after complete");
    completion_reset(&c);
    KT_CHECK(!completion_done(&c), "reset clears done");

    KT_RESULT("mutex tests");
}

static void test_rwlock(void) {
    KT_BEGIN();
    struct rwlock rw;
    rwlock_init(&rw, "test");

    /* Multiple readers. */
    KT_CHECK(rwlock_read_trylock(&rw), "reader 1");
    KT_CHECK(rwlock_read_trylock(&rw), "reader 2");
    KT_CHECK_EQ(rwlock_reader_count(&rw), 2, "two readers");
    /* Writer cannot acquire while readers hold. */
    KT_CHECK(!rwlock_write_trylock(&rw), "writer blocked by readers");
    rwlock_read_unlock(&rw);
    rwlock_read_unlock(&rw);
    KT_CHECK_EQ(rwlock_reader_count(&rw), 0, "no readers");

    /* Exclusive writer. */
    KT_CHECK(rwlock_write_trylock(&rw), "writer acquires");
    KT_CHECK(rwlock_write_held(&rw), "write held");
    KT_CHECK(!rwlock_read_trylock(&rw), "reader blocked by writer");
    KT_CHECK(!rwlock_write_trylock(&rw), "second writer blocked");
    rwlock_write_unlock(&rw);
    KT_CHECK(!rwlock_write_held(&rw), "write released");
    KT_CHECK(rwlock_read_trylock(&rw), "reader after write release");
    rwlock_read_unlock(&rw);

    KT_RESULT("rwlock tests");
}

void sync_tests_run(void) {
    test_spinlock();
    test_waitqueue();
    test_mutex();
    test_rwlock();
}
