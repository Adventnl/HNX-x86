/*
 * Thin convenience wrappers over the UEFI Boot Services memory routines.
 * Implemented in memory.c. All allocations use EfiLoaderData so the regions
 * survive ExitBootServices (the minimal kernel never reclaims them).
 */
#ifndef MYOS_BOOT_SERVICES_H
#define MYOS_BOOT_SERVICES_H

#include "efi.h"

#define EFI_PAGE_SIZE  0x1000ULL
#define EFI_PAGE_MASK  0xFFFULL
#define EFI_SIZE_TO_PAGES(sz) (((sz) + EFI_PAGE_MASK) / EFI_PAGE_SIZE)

/* Allocate `pages` 4 KiB pages anywhere; returns physical base or 0. */
void *bs_alloc_pages(UINTN pages);

/* Allocate `pages` 4 KiB pages at a fixed physical address. */
EFI_STATUS bs_alloc_pages_at(EFI_PHYSICAL_ADDRESS addr, UINTN pages);

/* Allocate `size` bytes from pool; returns pointer or NULL. */
void *bs_alloc_pool(UINTN size);

/* Free a pool allocation. */
void bs_free_pool(void *p);

/* Freestanding memory primitives (also satisfy compiler-emitted calls). */
void *memset(void *dst, int val, __SIZE_TYPE__ n);
void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n);
int   memcmp(const void *a, const void *b, __SIZE_TYPE__ n);

#endif /* MYOS_BOOT_SERVICES_H */
