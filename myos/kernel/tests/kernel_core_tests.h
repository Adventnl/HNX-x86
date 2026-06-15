/* Work Unit A: kernel core production foundation init + test entry points. */
#ifndef MYOS_KERNEL_CORE_TESTS_H
#define MYOS_KERNEL_CORE_TESTS_H

/* Bring up the core infrastructure (slab allocator, kobject model, debug log
 * ring, trace, symbol registry, dump framework). Safe to call once heap + PMM
 * are online; does not depend on the scheduler. */
void kernel_core_init(void);

/* Run the full Work Unit A test matrix and print the per-suite [PASS] markers
 * plus the "[OK] Kernel core production foundation online" line. */
void kernel_core_tests_run(void);

/* Individual suites (exposed for reuse by the test infrastructure unit). */
void lib_tests_run(void);
void allocator_tests_run(void);
void slab_tests_run(void);
void vm_tests_run(void);
void sync_tests_run(void);
void workqueue_tests_run(void);
void timer_tests_run(void);
void debug_tests_run(void);

#endif /* MYOS_KERNEL_CORE_TESTS_H */
