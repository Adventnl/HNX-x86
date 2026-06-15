/* Tiny stdio over the write syscall: printf + helpers. No buffering, no FILE. */
#ifndef MYOS_USER_STDIO_H
#define MYOS_USER_STDIO_H

#include "types.h"

int  printf(const char *fmt, ...);
int  puts(const char *s);          /* writes s + '\n' */
int  putchar(int c);
int  fputs(const char *s, int fd); /* write(fd, s, strlen(s)) */

int  snprintf(char *buf, size_t size, const char *fmt, ...);
int  vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap);

/* Read one line (including any trailing '\n') from fd into buf; returns the
 * number of bytes stored (0 at EOF with nothing read, <0 on error). The result
 * is always NUL-terminated when size > 0. */
long fdgets(int fd, char *buf, size_t size);

/* Convenience helpers used throughout the userland. */
long print(const char *s);         /* write(1, s, strlen(s)) */
long eprint(const char *s);        /* write(2, ...) */
void print_u64(uint64_t value);
void print_i64(int64_t value);

#endif /* MYOS_USER_STDIO_H */
