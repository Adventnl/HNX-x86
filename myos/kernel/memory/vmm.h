/* Virtual memory manager (kernel address space). */
#ifndef MYOS_VMM_H
#define MYOS_VMM_H

#include "types.h"
#include "boot_info.h"

/* Build the kernel-owned page tables (identity map + higher half). */
void vmm_init(const struct boot_info *boot_info);

/* Activate the kernel page tables (load CR3). */
void vmm_load_kernel_address_space(void);

/* Whether vmm_load_kernel_address_space() succeeded (custom CR3 active). */
int vmm_cr3_loaded(void);

/* Physical address of the kernel PML4. */
uint64_t vmm_kernel_pml4(void);

int      vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);
int      vmm_unmap_page(uint64_t virtual_address);
uint64_t vmm_get_physical(uint64_t virtual_address);

/* Post-CR3 sanity check: read back CR3, confirm every required region is
 * mapped (and identity-correct), and round-trip a scratch page. Returns 1 on
 * success, 0 on failure. */
int      vmm_validate_required_mappings(const struct boot_info *boot_info);

/* Re-map one identity 2 MiB page as MMIO (cache-disable + write-through).
 * `phys` must be 2 MiB aligned. Used for the Local APIC window. Returns 0 on
 * success. */
int      vmm_map_mmio_2m(uint64_t phys);

#endif /* MYOS_VMM_H */
