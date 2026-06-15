/* Tests for the deferred work queue. Marker: "workqueue tests". */
#include "ktest.h"
#include "workqueue.h"

static int g_counter;
static struct work *g_requeue_target;
static struct workqueue *g_requeue_wq;

static void inc_work(struct work *self, void *arg) {
    (void)self;
    g_counter += *(int *)arg;
}

static void requeue_work(struct work *self, void *arg) {
    (void)arg;
    /* A work item may re-queue another item while running. */
    if (g_requeue_target && g_requeue_wq) {
        workqueue_queue(g_requeue_wq, g_requeue_target);
    }
    (void)self;
}

void workqueue_tests_run(void) {
    KT_BEGIN();
    struct workqueue wq;
    workqueue_init(&wq, "test");
    KT_CHECK_EQ(workqueue_pending_count(&wq), 0, "empty wq");

    g_counter = 0;
    int amounts[4] = { 1, 2, 4, 8 };
    struct work items[4];
    for (int i = 0; i < 4; i++) {
        work_init(&items[i], inc_work, &amounts[i]);
        KT_CHECK_EQ(workqueue_queue(&wq, &items[i]), 1, "queue new item");
    }
    KT_CHECK_EQ(workqueue_pending_count(&wq), 4, "four pending");

    /* Double-queue of a pending item is a no-op. */
    KT_CHECK_EQ(workqueue_queue(&wq, &items[0]), 0, "double-queue rejected");

    int ran = workqueue_run_pending(&wq);
    KT_CHECK_EQ(ran, 4, "ran four items");
    KT_CHECK_EQ(g_counter, 15, "sum 1+2+4+8");
    KT_CHECK_EQ(workqueue_pending_count(&wq), 0, "drained");

    /* Cancel before run. */
    work_init(&items[0], inc_work, &amounts[0]);
    workqueue_queue(&wq, &items[0]);
    KT_CHECK_EQ(workqueue_cancel(&wq, &items[0]), 1, "cancel pending");
    KT_CHECK_EQ(workqueue_pending_count(&wq), 0, "cancelled item gone");
    KT_CHECK_EQ(workqueue_cancel(&wq, &items[0]), 0, "cancel non-pending");

    /* Re-queue from within a running item. */
    g_counter = 0;
    static struct work producer;
    static struct work consumer;
    work_init(&consumer, inc_work, &amounts[2]);   /* adds 4 */
    work_init(&producer, requeue_work, NULL);
    g_requeue_target = &consumer;
    g_requeue_wq = &wq;
    workqueue_queue(&wq, &producer);
    ran = workqueue_run_pending(&wq);
    KT_CHECK(ran >= 2, "producer + re-queued consumer ran");
    KT_CHECK_EQ(g_counter, 4, "re-queued consumer executed");

    KT_CHECK_EQ(wq.executed, (uint64_t)(4 + ran), "executed stat");
    KT_RESULT("workqueue tests");
}
