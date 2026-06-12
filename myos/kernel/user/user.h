/* User/kernel boundary: shared constants for the ring-3 subsystem (Prompt 4).
 *
 * Address-space layout (per user task, below the non-canonical hole):
 *   0x0000000000400000  USER_IMAGE_BASE  - HXE1 image is linked/loaded here
 *   ...                                    (code RX, data/bss RW, user pages)
 *   USER_STACK_TOP - SIZE .. USER_STACK_TOP  - user stack (RW, user pages)
 *   < USER_TOP                              - all user virtual addresses
 *
 * The kernel keeps running from its identity-mapped low memory, so every user
 * address space also mirrors the kernel's low footprint, the framebuffer, and
 * the Local APIC MMIO window (see user_address_space.c). USER_IMAGE_BASE sits
 * exactly at the 2 MiB boundary above the kernel image so the two never
 * overlap. */
#ifndef MYOS_USER_H
#define MYOS_USER_H

#include "types.h"

#define USER_IMAGE_BASE  0x0000000000400000ULL
#define USER_STACK_TOP   0x00007FFFFFFFE000ULL
#define USER_STACK_SIZE  0x0000000000020000ULL   /* 128 KiB */
#define USER_TOP         0x0000800000000000ULL   /* 128 TiB (canonical low half) */

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
