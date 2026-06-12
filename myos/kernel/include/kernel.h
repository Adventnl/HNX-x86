/* Core kernel declarations. */
#ifndef MYOS_KERNEL_H
#define MYOS_KERNEL_H

#include "types.h"
#include "boot_info.h"

/* Kernel C entry, called from arch/x86_64/entry.S with boot_info in RDI. */
void kernel_main(struct boot_info *bi);

/* The boot_info pointer captured by kernel_main (for subsystems/tests). */
const struct boot_info *kernel_boot_info(void);

/* Stop the CPU forever (cli; hlt loop). */
void khalt(void) __attribute__((noreturn));

/* Print a message (if a console is up) and halt. */
void panic(const char *msg) __attribute__((noreturn));

/* Freestanding memory/string primitives (string.c). */
void  *memset(void *dst, int val, usize n);
void  *memcpy(void *dst, const void *src, usize n);
int    memcmp(const void *a, const void *b, usize n);
usize  kstrlen(const char *s);

#endif /* MYOS_KERNEL_H */
