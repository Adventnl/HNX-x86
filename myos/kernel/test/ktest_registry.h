/* Kernel test registry: register named unit/integration tests and run them as
 * a suite with a structured result summary.
 *
 * Distinct from kernel/tests/ktest.h (which is the per-test [PASS]/[FAIL] macro
 * harness): this is the runner that collects tests, executes them, and reports
 * counts. A test function returns 1 on pass, 0 on fail. */
#ifndef MYOS_TEST_KTEST_REGISTRY_H
#define MYOS_TEST_KTEST_REGISTRY_H

#include "types.h"

#define KTEST_MAX 128

enum ktest_kind { KTEST_UNIT = 0, KTEST_INTEGRATION = 1 };

typedef int (*ktest_func)(void);

struct ktest_case {
    const char     *name;
    ktest_func      fn;
    enum ktest_kind kind;
};

struct ktest_result {
    uint32_t total;
    uint32_t passed;
    uint32_t failed;
    uint32_t unit;
    uint32_t integration;
};

void ktest_registry_init(void);
int  ktest_register(const char *name, ktest_func fn, enum ktest_kind kind);
uint32_t ktest_registered(void);

/* Run every registered test; fill *out (if non-NULL). Returns failed count. */
int  ktest_run_all(struct ktest_result *out);
/* Run only one kind. */
int  ktest_run_kind(enum ktest_kind kind, struct ktest_result *out);

#endif /* MYOS_TEST_KTEST_REGISTRY_H */
