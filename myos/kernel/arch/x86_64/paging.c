/* Low-level x86-64 page-table walk/map.
 *
 * All table pages are accessed by their physical address: low physical memory
 * is identity-mapped (by the UEFI map while building, and by our own identity
 * map once CR3 is loaded), so physical == virtual for table access. */
#include "paging.h"
#include "memory_layout.h"
#include "pmm.h"
#include "cpu.h"
#include "panic.h"

static inline uint64_t *table_ptr(uint64_t phys) {
    return (uint64_t *)(uintptr_t)phys;
}

static inline uint64_t idx_pml4(uint64_t va) { return (va >> 39) & 0x1FF; }
static inline uint64_t idx_pdpt(uint64_t va) { return (va >> 30) & 0x1FF; }
static inline uint64_t idx_pd(uint64_t va)   { return (va >> 21) & 0x1FF; }
static inline uint64_t idx_pt(uint64_t va)   { return (va >> 12) & 0x1FF; }

uint64_t paging_new_table(void) {
    uint64_t phys = pmm_alloc_page();
    if (phys == PMM_INVALID_PAGE) {
        kernel_panic("paging: out of pages for table");
    }
    uint64_t *t = table_ptr(phys);
    for (int i = 0; i < 512; i++) {
        t[i] = 0;
    }
    return phys;
}

/* Return the physical address of the next-level table for `va`, creating it if
 * needed. `level_flags` are applied to newly created intermediate entries. */
static uint64_t next_level(uint64_t *table, uint64_t index, uint64_t level_flags) {
    if (!(table[index] & PAGE_PRESENT)) {
        uint64_t next = paging_new_table();
        table[index] = (next & PAGE_ADDR_MASK) | PAGE_PRESENT | level_flags;
        return next;
    }
    return table[index] & PAGE_ADDR_MASK;
}

kstatus_t paging_map_4k(uint64_t pml4_phys, uint64_t va, uint64_t pa, uint64_t flags) {
    uint64_t inter = PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);

    uint64_t *pml4 = table_ptr(pml4_phys);
    uint64_t pdpt_phys = next_level(pml4, idx_pml4(va), inter);
    uint64_t *pdpt = table_ptr(pdpt_phys);
    uint64_t pd_phys = next_level(pdpt, idx_pdpt(va), inter);
    uint64_t *pd = table_ptr(pd_phys);
    if (pd[idx_pd(va)] & PAGE_HUGE) {
        return K_ERR_EXISTS;   /* a 2 MiB page already covers this range */
    }
    uint64_t pt_phys = next_level(pd, idx_pd(va), inter);
    uint64_t *pt = table_ptr(pt_phys);

    pt[idx_pt(va)] = (pa & PAGE_ADDR_MASK) | (flags & ~PAGE_HUGE) | PAGE_PRESENT;
    x86_invlpg((void *)(uintptr_t)va);
    return K_OK;
}

kstatus_t paging_map_2m(uint64_t pml4_phys, uint64_t va, uint64_t pa, uint64_t flags) {
    if ((va & LARGE_PAGE_MASK) || (pa & LARGE_PAGE_MASK)) {
        return K_ERR_ALIGN;
    }
    uint64_t inter = PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);

    uint64_t *pml4 = table_ptr(pml4_phys);
    uint64_t pdpt_phys = next_level(pml4, idx_pml4(va), inter);
    uint64_t *pdpt = table_ptr(pdpt_phys);
    uint64_t pd_phys = next_level(pdpt, idx_pdpt(va), inter);
    uint64_t *pd = table_ptr(pd_phys);

    pd[idx_pd(va)] = (pa & PAGE_ADDR_MASK) | (flags & ~PAGE_HUGE) | PAGE_PRESENT | PAGE_HUGE;
    x86_invlpg((void *)(uintptr_t)va);
    return K_OK;
}

kstatus_t paging_unmap_4k(uint64_t pml4_phys, uint64_t va) {
    uint64_t *pml4 = table_ptr(pml4_phys);
    if (!(pml4[idx_pml4(va)] & PAGE_PRESENT)) return K_ERR_NOTFOUND;
    uint64_t *pdpt = table_ptr(pml4[idx_pml4(va)] & PAGE_ADDR_MASK);
    if (!(pdpt[idx_pdpt(va)] & PAGE_PRESENT)) return K_ERR_NOTFOUND;
    uint64_t *pd = table_ptr(pdpt[idx_pdpt(va)] & PAGE_ADDR_MASK);
    if (!(pd[idx_pd(va)] & PAGE_PRESENT) || (pd[idx_pd(va)] & PAGE_HUGE)) return K_ERR_NOTFOUND;
    uint64_t *pt = table_ptr(pd[idx_pd(va)] & PAGE_ADDR_MASK);
    if (!(pt[idx_pt(va)] & PAGE_PRESENT)) return K_ERR_NOTFOUND;

    pt[idx_pt(va)] = 0;
    x86_invlpg((void *)(uintptr_t)va);
    return K_OK;
}

uint64_t paging_translate(uint64_t pml4_phys, uint64_t va) {
    uint64_t *pml4 = table_ptr(pml4_phys);
    if (!(pml4[idx_pml4(va)] & PAGE_PRESENT)) return PAGING_NO_MAP;
    uint64_t *pdpt = table_ptr(pml4[idx_pml4(va)] & PAGE_ADDR_MASK);
    if (!(pdpt[idx_pdpt(va)] & PAGE_PRESENT)) return PAGING_NO_MAP;
    uint64_t pdpte = pdpt[idx_pdpt(va)];
    if (pdpte & PAGE_HUGE) {   /* 1 GiB page */
        return (pdpte & PAGE_ADDR_MASK) | (va & 0x3FFFFFFF);
    }
    uint64_t *pd = table_ptr(pdpte & PAGE_ADDR_MASK);
    if (!(pd[idx_pd(va)] & PAGE_PRESENT)) return PAGING_NO_MAP;
    uint64_t pde = pd[idx_pd(va)];
    if (pde & PAGE_HUGE) {     /* 2 MiB page */
        return (pde & PAGE_ADDR_MASK) | (va & LARGE_PAGE_MASK);
    }
    uint64_t *pt = table_ptr(pde & PAGE_ADDR_MASK);
    if (!(pt[idx_pt(va)] & PAGE_PRESENT)) return PAGING_NO_MAP;
    return (pt[idx_pt(va)] & PAGE_ADDR_MASK) | (va & PAGE_MASK);
}

void paging_load_cr3(uint64_t pml4_phys) {
    x86_write_cr3(pml4_phys);
}
