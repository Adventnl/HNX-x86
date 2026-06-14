/* Physical memory manager (bitmap allocator). */
#ifndef MYOS_PMM_H
#define MYOS_PMM_H

#include "types.h"
#include "boot_info.h"

/* Kernel-side copy of a UEFI memory descriptor. */
struct efi_memory_descriptor_kernel {
    uint32_t type;
    uint32_t padding;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
};

/* UEFI memory types we care about. */
#define EFI_CONVENTIONAL_MEMORY      7
#define EFI_LOADER_CODE              1
#define EFI_LOADER_DATA              2
#define EFI_BOOT_SERVICES_CODE       3
#define EFI_BOOT_SERVICES_DATA       4
#define EFI_ACPI_RECLAIM_MEMORY      9
#define EFI_ACPI_MEMORY_NVS         10
#define EFI_RUNTIME_SERVICES_CODE    5
#define EFI_RUNTIME_SERVICES_DATA    6
#define EFI_MEMORY_MAPPED_IO        11
#define EFI_MEMORY_MAPPED_IO_PORT   12

/* Whitelist of RAM-backed memory types. Reserved/MMIO/unusable regions (which
 * can sit at very high addresses, e.g. the 64-bit PCI hole) are excluded so the
 * bitmap and identity map are sized to real RAM only. */
static inline int efi_type_is_ram(uint32_t type) {
    switch (type) {
    case EFI_LOADER_CODE:
    case EFI_LOADER_DATA:
    case EFI_BOOT_SERVICES_CODE:
    case EFI_BOOT_SERVICES_DATA:
    case EFI_RUNTIME_SERVICES_CODE:
    case EFI_RUNTIME_SERVICES_DATA:
    case EFI_CONVENTIONAL_MEMORY:
    case EFI_ACPI_RECLAIM_MEMORY:
    case EFI_ACPI_MEMORY_NVS:
        return 1;
    default:
        return 0;
    }
}

#define PMM_INVALID_PAGE 0ULL   /* page 0 is always reserved -> used as error */

void     pmm_init(const struct boot_info *boot_info);
uint64_t pmm_alloc_page(void);
void     pmm_free_page(uint64_t physical_address);

/* Allocate `count` physically-contiguous 4 KiB pages (for DMA: command lists,
 * FIS areas, PRDTs, bounce buffers). Returns the base physical address (also
 * the identity-mapped virtual address) or PMM_INVALID_PAGE. */
uint64_t pmm_alloc_contig(uint64_t count);
void     pmm_free_contig(uint64_t physical_address, uint64_t count);

uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);
uint64_t pmm_used_pages(void);
uint64_t pmm_reserved_pages(void);     /* pages reserved at init (never freed)  */
uint64_t pmm_bitmap_base(void);        /* physical address of the bitmap        */
uint64_t pmm_bitmap_size(void);        /* bitmap size in bytes                  */
uint64_t pmm_lowest_page(void);        /* lowest allocatable physical address   */
uint64_t pmm_highest_address(void);    /* highest managed RAM address           */
void     pmm_dump_stats(void);

/* Stress test: allocate 64 pages, verify alignment/uniqueness, free them, and
 * confirm the free count is restored. Returns 1 on success, 0 on failure. */
int      pmm_stress_test(int verbose);

#endif /* MYOS_PMM_H */
