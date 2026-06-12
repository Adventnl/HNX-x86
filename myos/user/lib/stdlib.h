/* Tiny user convenience layer built on the syscalls (no host libc). */
#ifndef MYOS_USER_STDLIB_H
#define MYOS_USER_STDLIB_H

#include "start.h"

/* Write a NUL-terminated string to stdout (fd 1). Returns the syscall result. */
long print(const char *s);

/* Write a NUL-terminated string to stderr (fd 2). */
long eprint(const char *s);

/* Print an unsigned 64-bit value in decimal to stdout. */
void print_u64(uint64_t value);

/* Print a signed 64-bit value in decimal to stdout. */
void print_i64(int64_t value);

#endif /* MYOS_USER_STDLIB_H */
