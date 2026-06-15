/* Kernel test registry implementation (see kernel/test/ktest_registry.h). */
#include "ktest_registry.h"
#include "log.h"
#include "fmt.h"

static struct ktest_case g_cases[KTEST_MAX];
static uint32_t          g_count;

void ktest_registry_init(void) {
    g_count = 0;
}

int ktest_register(const char *name, ktest_func fn, enum ktest_kind kind) {
    if (g_count >= KTEST_MAX || !fn) {
        return -1;
    }
    g_cases[g_count].name = name;
    g_cases[g_count].fn = fn;
    g_cases[g_count].kind = kind;
    g_count++;
    return 0;
}

uint32_t ktest_registered(void) {
    return g_count;
}

static int run_filtered(int kind_filter, enum ktest_kind kind,
                        struct ktest_result *out) {
    struct ktest_result r = {0, 0, 0, 0, 0};
    for (uint32_t i = 0; i < g_count; i++) {
        if (kind_filter && g_cases[i].kind != kind) {
            continue;
        }
        r.total++;
        if (g_cases[i].kind == KTEST_UNIT) {
            r.unit++;
        } else {
            r.integration++;
        }
        int ok = g_cases[i].fn();
        if (ok) {
            r.passed++;
        } else {
            r.failed++;
            kdprintf("    [ktest] FAILED: %s\n", g_cases[i].name);
        }
    }
    if (out) {
        *out = r;
    }
    return (int)r.failed;
}

int ktest_run_all(struct ktest_result *out) {
    return run_filtered(0, KTEST_UNIT, out);
}

int ktest_run_kind(enum ktest_kind kind, struct ktest_result *out) {
    return run_filtered(1, kind, out);
}
