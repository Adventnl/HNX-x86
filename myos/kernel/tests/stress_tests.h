/* Work Unit H: kernel test infrastructure + fuzz/stress entry point. */
#ifndef MYOS_STRESS_TESTS_H
#define MYOS_STRESS_TESTS_H

/* Runs the kernel test registry demo, the allocator/USB-corpus/packet
 * randomized + stress suites, and the kernel-side syscall stress; prints the
 * Work Unit H markers and "[OK] Test infrastructure online". */
void test_infra_run(void);

#endif /* MYOS_STRESS_TESTS_H */
