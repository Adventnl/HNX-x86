/* Tests for the timer callout wheel: arming, ordered firing, cancellation,
 * periodic re-arm and missed-tick catch-up. Uses a virtual clock so the test
 * is deterministic. Marker: "timer callout tests". */
#include "ktest.h"
#include "callout.h"

static int g_fired[8];
static int g_fire_order[16];
static int g_fire_idx;

static void fire_cb(struct callout *c, void *arg) {
    (void)c;
    int id = (int)(uintptr_t)arg;
    if (id >= 0 && id < 8) {
        g_fired[id]++;
    }
    if (g_fire_idx < 16) {
        g_fire_order[g_fire_idx++] = id;
    }
}

void timer_tests_run(void) {
    KT_BEGIN();
    struct callout_base base;
    callout_base_init(&base);
    for (int i = 0; i < 8; i++) {
        g_fired[i] = 0;
    }
    g_fire_idx = 0;

    struct callout c[4];
    for (int i = 0; i < 4; i++) {
        callout_init(&c[i], fire_cb, (void *)(uintptr_t)i);
    }

    /* Arm out of order: expiries 30, 10, 40, 20. */
    callout_arm(&base, &c[0], 30);
    callout_arm(&base, &c[1], 10);
    callout_arm(&base, &c[2], 40);
    callout_arm(&base, &c[3], 20);
    KT_CHECK_EQ(callout_armed(&base), 4, "four armed");
    KT_CHECK_EQ(callout_next_expiry(&base), 10, "earliest expiry first");

    /* Advance to t=15: only c1 (exp 10) fires. */
    KT_CHECK_EQ(callout_advance(&base, 15), 1, "one fired at t=15");
    KT_CHECK(g_fired[1] == 1, "c1 fired");
    KT_CHECK_EQ(callout_armed(&base), 3, "three remain");

    /* Advance to t=35: c3 (20) and c0 (30) fire, in expiry order. */
    KT_CHECK_EQ(callout_advance(&base, 35), 2, "two fired at t=35");
    KT_CHECK(g_fired[3] == 1 && g_fired[0] == 1, "c3 and c0 fired");
    /* Order of those two firings was 3 then 0 (20 before 30). */
    KT_CHECK(g_fire_order[1] == 3 && g_fire_order[2] == 0, "expiry order honored");

    /* Cancel c2 before it fires. */
    KT_CHECK_EQ(callout_cancel(&base, &c[2]), 1, "cancel armed");
    KT_CHECK_EQ(callout_advance(&base, 100), 0, "cancelled does not fire");
    KT_CHECK(g_fired[2] == 0, "c2 never fired");
    KT_CHECK_EQ(callout_armed(&base), 0, "none armed");

    /* Periodic: fire every 10 ticks starting at +10. */
    struct callout p;
    callout_init(&p, fire_cb, (void *)(uintptr_t)5);
    callout_arm_periodic(&base, &p, 10, 10);
    int total = 0;
    total += callout_advance(&base, 110);   /* fires at 110 */
    total += callout_advance(&base, 130);   /* fires at 120,130 */
    KT_CHECK(total >= 3, "periodic fired multiple times");
    KT_CHECK(p.fire_count >= 3, "periodic fire_count");

    /* Catch-up: a large jump fires one firing per elapsed period and leaves the
     * timer re-armed strictly in the future. */
    uint64_t before = p.fire_count;
    int caught = callout_advance(&base, 1000);
    KT_CHECK(caught >= 1, "catch-up fired at least once");
    KT_CHECK(p.fire_count > before, "catch-up advanced fire_count");
    KT_CHECK(callout_next_expiry(&base) > 1000, "periodic re-armed in the future");
    callout_cancel(&base, &p);

    KT_RESULT("timer callout tests");
}
