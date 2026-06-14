/* Safe kernel<->user memory copies + a kernel-CR3 helper.
 *
 * A syscall runs with the calling process's CR3 active, so user virtual
 * addresses are directly addressable once validated against that process's page
 * tables (user_range_is_valid). A bad pointer returns -EFAULT; the kernel never
 * dereferences unvalidated user memory.
 *
 * Operations that touch arbitrary physical RAM (page-table allocation, copying
 * an image into a child's frames) must instead run with the kernel CR3 active so
 * the full identity map is available; user_with_kernel_cr3 / user_restore_cr3
 * bracket such non-blocking sections. */
#ifndef MYOS_USER_COPY_H
#define MYOS_USER_COPY_H

#include "types.h"

/* Copy `n` bytes from a user pointer into kernel memory. Returns 0 or -EFAULT. */
int user_copy_from_user(void *kdst, uint64_t usrc, uint64_t n);

/* Copy `n` bytes from kernel memory to a user pointer. Returns 0 or -EFAULT. */
int user_copy_to_user(uint64_t udst, const void *ksrc, uint64_t n);

/* Duplicate a NUL-terminated user string into a fresh kernel buffer (kmalloc'd,
 * caller frees). At most `maxlen` bytes incl. the terminator. NULL on fault or
 * overflow. */
char *user_copy_string_from_user(uint64_t usrc, uint64_t maxlen);

/* Switch to the kernel address space for a short, non-blocking critical section
 * (no sleeping/yielding while held). Returns the previously active CR3. */
uint64_t user_with_kernel_cr3(void);
void     user_restore_cr3(uint64_t saved);

#endif /* MYOS_USER_COPY_H */
