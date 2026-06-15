/* Minimal formatted output for the freestanding kernel.
 *
 * Supports a practical subset of printf: %d %i %u %x %X %p %s %c %% with field
 * width, '0' / '-' / '+' / ' ' flags, and the 'l'/'ll'/'z' length modifiers.
 * No floating point (the kernel is built -msoft-float and never touches FP).
 */
#ifndef MYOS_LIB_FMT_H
#define MYOS_LIB_FMT_H

#include "types.h"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#define va_copy(d, s)      __builtin_va_copy(d, s)

/* Write into buf (always NUL-terminated when size > 0). Returns the number of
 * characters that *would* have been written (excluding the terminator), so a
 * return >= size means truncation occurred. */
int ksnprintf(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

/* Format and route through kernel_log() (no trailing newline added). */
int kdprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Helpers used by diagnostics: format an unsigned value into a small buffer. */
int  kfmt_u64(char *buf, size_t size, uint64_t v, unsigned base, int upper);
void kfmt_hex64(char *buf, size_t size, uint64_t v); /* "0x" + 16 hex digits */

#endif /* MYOS_LIB_FMT_H */
