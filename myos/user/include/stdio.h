/* Tiny stdio over the write syscall: printf + helpers. No buffering, no FILE. */
#ifndef MYOS_USER_STDIO_H
#define MYOS_USER_STDIO_H

#include "types.h"

int  printf(const char *fmt, ...);
int  puts(const char *s);          /* writes s + '\n' */
int  putchar(int c);

/* Convenience helpers used throughout the userland. */
long print(const char *s);         /* write(1, s, strlen(s)) */
long eprint(const char *s);        /* write(2, ...) */
void print_u64(uint64_t value);
void print_i64(int64_t value);

#endif /* MYOS_USER_STDIO_H */
