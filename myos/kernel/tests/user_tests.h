/* Kernel-side user/kernel boundary tests: a supervisor thread that launches the
 * initramfs user programs, waits for them, and validates their exit codes. */
#ifndef MYOS_USER_TESTS_H
#define MYOS_USER_TESTS_H

void user_tests_start(void);

#endif /* MYOS_USER_TESTS_H */
