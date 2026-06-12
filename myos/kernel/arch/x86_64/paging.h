/* Low-level x86-64 page-table primitives. */
#ifndef MYOS_X86_PAGING_H
#define MYOS_X86_PAGING_H

#include "types.h"
#include "status.h"

/* Page-table entry flags. */
#define PAGE_PRESENT       (1ULL << 0)
#define PAGE_WRITABLE      (1ULL << 1)
#define PAGE_USER          (1ULL << 2)
#define PAGE_WRITE_THROUGH (1ULL << 3)
#define PAGE_CACHE_DISABLE (1ULL << 4)
#define PAGE_ACCESSED      (1ULL << 5)
#define PAGE_DIRTY         (1ULL << 6)
#define PAGE_HUGE          (1ULL << 7)   /* PS bit (2 MiB / 1 GiB pages) */
#define PAGE_GLOBAL        (1ULL << 8)
#define PAGE_NO_EXECUTE    (1ULL << 63)

#define PAGE_ADDR_MASK     0x000FFFFFFFFFF000ULL

#define PAGING_NO_MAP      (~0ULL)

/* Allocate a zeroed page table from the PMM; returns physical address. */
uint64_t paging_new_table(void);

/* Map a single 4 KiB page in the address space rooted at `pml4_phys`. */
kstatus_t paging_map_4k(uint64_t pml4_phys, uint64_t va, uint64_t pa, uint64_t flags);

/* Map a single 2 MiB large page. va/pa must be 2 MiB aligned. */
kstatus_t paging_map_2m(uint64_t pml4_phys, uint64_t va, uint64_t pa, uint64_t flags);

/* Remove a 4 KiB mapping. */
kstatus_t paging_unmap_4k(uint64_t pml4_phys, uint64_t va);

/* Translate a virtual address; returns physical or PAGING_NO_MAP. */
uint64_t paging_translate(uint64_t pml4_phys, uint64_t va);

void paging_load_cr3(uint64_t pml4_phys);

#endif /* MYOS_X86_PAGING_H */
