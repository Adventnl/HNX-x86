/* User/kernel boundary: shared constants for the ring-3 subsystem.
 *
 * Address-space layout (per process, all in the canonical low half):
 *   0x0000000000400000  USER_IMAGE_BASE  - HXE1 image (code RX, data/bss RW)
 *   0x0000004000000000  USER_HEAP_BASE   - bump heap for the user malloc
 *   USER_STACK_TOP-SIZE .. USER_STACK_TOP - user stack (RW) + argv block
 *   < USER_TOP                            - all user virtual addresses
 *
 * Each address space mirrors the kernel's low footprint, the framebuffer, the
 * Local APIC MMIO window and the initramfs RAM (supervisor), so kernel code can
 * run (syscalls / IRQs / faults) and read file data while a user CR3 is active.
 * Page-table allocation that touches arbitrary physical RAM is instead done with
 * the kernel CR3 active (see user_copy.c: user_with_kernel_cr3). */
#ifndef MYOS_USER_H
#define MYOS_USER_H

#include "types.h"

#define USER_IMAGE_BASE  0x0000000000400000ULL
#define USER_STACK_TOP   0x00007FFFFFFFE000ULL
#define USER_STACK_SIZE  0x0000000000040000ULL   /* 256 KiB */
#define USER_HEAP_BASE   0x0000004000000000ULL    /* 256 GiB */
#define USER_HEAP_SIZE   0x0000000001000000ULL    /* 16 MiB virtual reservation */
#define USER_HEAP_INITIAL 0x0000000000100000ULL   /* 1 MiB pre-mapped on spawn */
#define USER_TOP         0x0000800000000000ULL    /* 128 TiB (canonical low half) */

/* Ring-3 segment selectors = GDT user data/code with RPL 3. */
#define USER_DATA_SELECTOR_RPL3 0x1B
#define USER_CODE_SELECTOR_RPL3 0x23

/* Validate the ring-3 GDT/TSS configuration and announce the ring-3 entry path.
 * Logs "[OK] Ring 3 segments validated" and "[OK] Ring 3 entry online". */
void user_init(void);

/* Enter ring 3 (defined in user_entry.S). Loads `user_cr3`, builds an iretq
 * frame (SS=0x1B, RSP=user_rsp, RFLAGS with IF set, CS=0x23, RIP=user_rip) and
 * never returns. */
void user_enter_ring3(uint64_t user_rip, uint64_t user_rsp, uint64_t user_cr3)
    __attribute__((noreturn));

#endif /* MYOS_USER_H */
