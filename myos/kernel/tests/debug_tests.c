/* Tests for kernel/debug + kernel/object: log ring, trace ring, symbol resolve,
 * backtrace capture, object refcounts/registry/handle table and the dump
 * framework. Marker: "debug dump tests". */
#include "ktest.h"
#include "klog_ring.h"
#include "ktrace.h"
#include "ksym.h"
#include "backtrace.h"
#include "dump.h"
#include "object.h"
#include "string.h"

static int g_released;
static void test_release(struct kobject *o) {
    (void)o;
    g_released++;
}

void debug_tests_run(void) {
    KT_BEGIN();

    /* Log ring captures and snapshots. */
    klog_ring_init();
    klog_ring_write("hello ");
    klog_ring_write("world\n");
    char buf[64];
    size_t n = klog_ring_snapshot(buf, sizeof(buf) - 1);
    buf[n] = '\0';
    KT_CHECK(strcmp(buf, "hello world\n") == 0, "log ring snapshot");
    KT_CHECK_EQ(klog_ring_used(), 12, "log ring used bytes");

    /* Trace ring records events newest-first. */
    ktrace_init();
    ktrace_enable(KTRACE_CAT_SCHED);
    KT_CHECK(ktrace_enabled(KTRACE_CAT_SCHED), "category enabled");
    KT_CHECK(!ktrace_enabled(KTRACE_CAT_NET), "category disabled");
    KTRACE(KTRACE_CAT_SCHED, 1, "switch", 100, 200);
    KTRACE(KTRACE_CAT_NET, 2, "drop", 0, 0);     /* disabled -> ignored */
    KTRACE(KTRACE_CAT_SCHED, 3, "switch", 300, 400);
    KT_CHECK_EQ(ktrace_count(), 2, "only enabled events recorded");
    struct ktrace_event ev[4];
    size_t got = ktrace_snapshot(ev, 4);
    KT_CHECK_EQ(got, 2, "snapshot count");
    KT_CHECK_EQ(ev[0].id, 3, "newest first");
    KT_CHECK_EQ(ev[0].arg0, 300, "event arg preserved");

    /* Symbol registry resolves to nearest preceding symbol. */
    ksym_init();
    ksym_add(0x2000, "beta");
    ksym_add(0x1000, "alpha");
    ksym_add(0x3000, "gamma");
    uint64_t off = 0;
    const char *name = ksym_resolve(0x1040, &off);
    KT_CHECK(name && strcmp(name, "alpha") == 0, "resolve alpha");
    KT_CHECK_EQ(off, 0x40, "resolve offset");
    name = ksym_resolve(0x2500, &off);
    KT_CHECK(name && strcmp(name, "beta") == 0, "resolve beta");
    KT_CHECK(ksym_resolve(0x500, &off) == NULL, "below all symbols");

    /* Backtrace capture returns at least this frame. */
    uint64_t frames[8];
    size_t fn = backtrace_capture(frames, 8, 0);
    KT_CHECK(fn >= 1, "backtrace captured a frame");

    /* Object refcounting + release-on-zero. */
    kobject_subsystem_init();
    g_released = 0;
    struct kobject o;
    kobject_init(&o, KOBJ_GENERIC, "obj", test_release);
    KT_CHECK_EQ(kobject_refs(&o), 1, "initial ref 1");
    kobject_get(&o);
    KT_CHECK_EQ(kobject_refs(&o), 2, "ref 2 after get");
    kobject_put(&o);
    KT_CHECK_EQ(g_released, 0, "not released at ref 1");
    kobject_put(&o);
    KT_CHECK_EQ(g_released, 1, "released at ref 0");

    /* Registry. */
    struct kobject a, b;
    kobject_init(&a, KOBJ_DEVICE, "a", NULL);
    kobject_init(&b, KOBJ_DEVICE, "b", NULL);
    size_t base = kobject_count();
    kobject_register(&a);
    kobject_register(&b);
    KT_CHECK_EQ(kobject_count(), base + 2, "registry count");
    KT_CHECK(a.id != 0 && b.id != a.id, "unique ids");
    KT_CHECK(kobject_lookup(a.id) == &a, "lookup by id");
    KT_CHECK(kobject_count_by_type(KOBJ_DEVICE) >= 2, "count by type");
    kobject_unregister(&a);
    kobject_unregister(&b);
    KT_CHECK_EQ(kobject_count(), base, "registry count restored");

    /* Handle table installs/looks-up/closes and manages references. */
    struct kobject h;
    kobject_init(&h, KOBJ_FILE, "file", NULL);
    struct handle_table tbl;
    KT_CHECK_EQ(handle_table_init(&tbl, 16), 0, "handle table init");
    int hd = handle_install(&tbl, &h, 0x7);
    KT_CHECK(hd >= 0, "handle installed");
    KT_CHECK_EQ(kobject_refs(&h), 2, "install took a reference");
    uint32_t rights = 0;
    KT_CHECK(handle_lookup(&tbl, hd, &rights) == &h, "handle lookup");
    KT_CHECK_EQ(rights, 0x7, "handle rights");
    KT_CHECK_EQ(handle_close(&tbl, hd), 0, "handle close");
    KT_CHECK_EQ(kobject_refs(&h), 1, "close dropped the reference");
    KT_CHECK(handle_lookup(&tbl, hd, NULL) == NULL, "closed handle gone");
    handle_table_destroy(&tbl);

    /* Dump framework registers core dumpers and runs without faulting. */
    debug_dump_init();
    KT_CHECK(debug_dumper_count() >= 5, "core dumpers registered");
    KT_CHECK_EQ(debug_dump_one("slab"), 0, "named dump runs");
    KT_CHECK_EQ(debug_dump_one("nonexistent"), -1, "unknown dump rejected");

    KT_RESULT("debug dump tests");
}
