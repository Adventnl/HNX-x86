/* Task State Segment (x86-64). */
#ifndef MYOS_X86_TSS_H
#define MYOS_X86_TSS_H

#include "types.h"

/* IST indices used by the IDT. */
#define IST_DOUBLE_FAULT 1
#define IST_PAGE_FAULT   2

struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

void tss_init(void);

#endif /* MYOS_X86_TSS_H */
