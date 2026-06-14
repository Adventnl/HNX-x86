/* Per-task user address space: a private PML4 that mirrors the kernel's low
 * footprint + framebuffer + LAPIC MMIO (so kernel code can run under it during
 * syscalls/IRQs) and additionally maps user pages below USER_TOP. */
#ifndef MYOS_USER_ADDRESS_SPACE_H
#define MYOS_USER_ADDRESS_SPACE_H

#include "types.h"

struct user_address_space;

/* Create a new address space (kernel mirror installed, no user pages yet), or
 * NULL on failure. */
struct user_address_space *user_address_space_create(void);

/* Free a space's page tables and user frames (kernel-shared frames untouched). */
void user_address_space_destroy(struct user_address_space *space);

/* Physical CR3 value (PML4 base) to load for this space. */
uint64_t user_address_space_cr3(struct user_address_space *space);

/* Map one already-allocated physical page at a user virtual address. `flags`
 * carries PAGE_WRITABLE (PAGE_USER and PAGE_PRESENT are added). Returns 0/err. */
int user_map_page(struct user_address_space *space, uint64_t virtual_address,
                  uint64_t physical_address, uint64_t flags);

/* Allocate + map + zero `size` bytes (page granular) of fresh user memory at
 * `virtual_address`. Returns 0/err. */
int user_map_range(struct user_address_space *space, uint64_t virtual_address,
                   uint64_t size, uint64_t flags);

/* Unmap + free `size` bytes (page granular) of user memory at `virtual_address`.
 * Returns 0/err. Only user (PAGE_USER) leaves are freed. */
int user_unmap_range(struct user_address_space *space, uint64_t virtual_address,
                     uint64_t size);

/* Copy `size` bytes from kernel memory into already-mapped user pages (written
 * through the frames' identity addresses, so the kernel CR3 must be active). */
int user_copy_to_space(struct user_address_space *space, uint64_t user_dst,
                       const void *kernel_src, uint64_t size);

/* Copy `size` bytes out of already-mapped user pages into kernel memory (read
 * through the frames' identity addresses; kernel CR3 must be active). */
int user_copy_from_space(struct user_address_space *space, void *kernel_dst,
                         uint64_t user_src, uint64_t size);

/* Zero `size` bytes of already-mapped user memory. */
int user_zero_in_space(struct user_address_space *space, uint64_t user_dst,
                       uint64_t size);

/* Whether [user_address, user_address+size) is entirely user-accessible
 * (mapped, PAGE_USER, and PAGE_WRITABLE if require_write). Returns 1/0. */
int user_range_is_valid(struct user_address_space *space, uint64_t user_address,
                        uint64_t size, int require_write);

#endif /* MYOS_USER_ADDRESS_SPACE_H */
