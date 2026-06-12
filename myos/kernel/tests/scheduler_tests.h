/* Scheduler self-tests (threads A/B/C + checker). */
#ifndef MYOS_SCHEDULER_TESTS_H
#define MYOS_SCHEDULER_TESTS_H

/* Create the test threads. They run once the scheduler starts; the checker
 * thread prints [TEST]/[PASS] lines and "[OK] Scheduler tests passed". */
void scheduler_tests_start(void);

#endif /* MYOS_SCHEDULER_TESTS_H */
