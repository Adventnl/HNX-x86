/* Global Descriptor Table (x86-64). */
#ifndef MYOS_X86_GDT_H
#define MYOS_X86_GDT_H

#include "types.h"

/* Segment selectors. */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18
#define GDT_USER_CODE   0x20
#define GDT_TSS         0x28

void gdt_init(void);

/* Install the 64-bit TSS system descriptor (entries 5+6). Called by tss_init. */
void gdt_set_tss(uint64_t base, uint32_t limit);

#endif /* MYOS_X86_GDT_H */
