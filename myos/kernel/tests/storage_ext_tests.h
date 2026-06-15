#ifndef MYOS_STORAGE_EXT_TESTS_H
#define MYOS_STORAGE_EXT_TESTS_H
/* Storage/driver production-foundation self-tests: driver lifecycle, PCI
 * capability walk, MSI/MSI-X discovery, AHCI identify + multi-sector I/O +
 * error handling, NVMe identify (honest-deferred block I/O), and the block-layer
 * async + statistics foundations. Runs at boot in kernel context. */
void storage_ext_tests_run(void);
#endif /* MYOS_STORAGE_EXT_TESTS_H */
