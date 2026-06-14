#ifndef MYOS_STORAGE_TESTS_H
#define MYOS_STORAGE_TESTS_H
/* Block cache + partition + disk read/write tests. */
void storage_tests_run(void);
/* HNXFS create/write/read/mkdir/unlink tests (run after /disk is mounted). */
void hnxfs_tests_run(void);
#endif
