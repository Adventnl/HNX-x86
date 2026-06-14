/* Executable loading: resolve an HXE1 path through the VFS and map it into a
 * user address space. Must run with the kernel CR3 active (touches arbitrary
 * physical RAM). */
#ifndef MYOS_PROCESS_EXEC_H
#define MYOS_PROCESS_EXEC_H

#include "types.h"

struct user_address_space;

/* Load the HXE1 at `path` (resolved relative to `cwd`) into `space`; writes the
 * entry point to *out_entry. Returns 0 or a negative error. */
int exec_load(const char *cwd, const char *path,
              struct user_address_space *space, uint64_t *out_entry);

#endif /* MYOS_PROCESS_EXEC_H */
