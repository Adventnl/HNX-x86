/* Kernel panic / halt. */
#ifndef MYOS_PANIC_H
#define MYOS_PANIC_H

#include "types.h"

/* Disable interrupts and stop the CPU forever. */
void kernel_halt_forever(void) __attribute__((noreturn));

/* Print a message through the logger (if available) and halt forever. */
void kernel_panic(const char *message) __attribute__((noreturn));

/* Like kernel_panic but also prints a 64-bit value. */
void kernel_panic_hex(const char *message, uint64_t value) __attribute__((noreturn));

#endif /* MYOS_PANIC_H */
