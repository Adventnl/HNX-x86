/* Per-task user address space.
 *
 * Each space is a fresh PML4 built under the (active) kernel CR3:
 *   1. The kernel low footprint [0, USER_IMAGE_BASE) is identity-mapped with
 *      supervisor 2 MiB pages so the kernel image, stacks, heap, GDT/IDT/TSS
 *      and IST stacks remain addressable when the CPU runs kernel code (syscall
 *      / IRQ / fault handlers) while this CR3 is active.
 *   2. The framebuffer and Local APIC MMIO windows are identity-mapped
 *      (cache-disable/write-through, supervisor) so logging and EOI work under
 *      this CR3.
 *   3. User pages (image + stack) are added below USER_TOP with PAGE_USER.
 *
 * Because USER_IMAGE_BASE sits at the 2 MiB boundary just above the kernel
 * image, user pages never overlap the kernel footprint. The PML4/PDPT entries
 * that lead to the user image are pre-created supervisor-only by step 1, so we
 * OR in PAGE_USER on those two intermediate levels (the 2 MiB leaves below them
 * stay supervisor-only, keeping kernel memory inaccessible to ring 3). */
#include "user_address_space.h"
#include "user.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "memory_layout.h"
#include "kernel.h"
#include "apic.h"
#include "log.h"
#include "panic.h"

struct user_address_space {
    uint64_t pml4;          /* physical base (== CR3 value) */
};

static inline uint64_t *phys_table(uint64_t phys) {
    return (uint64_t *)(uintptr_t)phys;
}
static inline uint64_t idx_pml4(uint64_t va) { return (va >> 39) & 0x1FF; }
static inline uint64_t idx_pdpt(uint64_t va) { return (va >> 30) & 0x1FF; }
static inline uint64_t idx_pd(uint64_t va)   { return (va >> 21) & 0x1FF; }
static inline uint64_t idx_pt(uint64_t va)   { return (va >> 12) & 0x1FF; }

/* Return the leaf PTE/PDE for `va`, or 0 if not present. Sets *is_2m. */
static uint64_t walk_leaf(uint64_t pml4_phys, uint64_t va, int *is_2m) {
    *is_2m = 0;
    uint64_t *pml4 = phys_table(pml4_phys);
    if (!(pml4[idx_pml4(va)] & PAGE_PRESENT)) return 0;
    uint64_t *pdpt = phys_table(pml4[idx_pml4(va)] & PAGE_ADDR_MASK);
    if (!(pdpt[idx_pdpt(va)] & PAGE_PRESENT)) return 0;
    if (pdpt[idx_pdpt(va)] & PAGE_HUGE) return pdpt[idx_pdpt(va)];   /* 1 GiB */
    uint64_t *pd = phys_table(pdpt[idx_pdpt(va)] & PAGE_ADDR_MASK);
    if (!(pd[idx_pd(va)] & PAGE_PRESENT)) return 0;
    if (pd[idx_pd(va)] & PAGE_HUGE) { *is_2m = 1; return pd[idx_pd(va)]; }
    uint64_t *pt = phys_table(pd[idx_pd(va)] & PAGE_ADDR_MASK);
    if (!(pt[idx_pt(va)] & PAGE_PRESENT)) return 0;
    return pt[idx_pt(va)];
}

/* Identity-map [phys, phys+span) into `pml4` with supervisor 2 MiB pages. */
static int map_supervisor_2m(uint64_t pml4, uint64_t phys, uint64_t span, uint64_t flags) {
    uint64_t start = LARGE_ALIGN_DOWN(phys);
    uint64_t end   = LARGE_ALIGN_UP(phys + span);
    for (uint64_t pa = start; pa < end; pa += LARGE_PAGE_SIZE) {
        if (paging_map_2m(pml4, pa, pa, flags) != K_OK) {
            return -1;
        }
    }
    return 0;
}

struct user_address_space *user_address_space_create(void) {
    const struct boot_info *bi = kernel_boot_info();
    uint64_t kernel_top = bi->kernel.kernel_base + bi->kernel.kernel_size;
    if (kernel_top > USER_IMAGE_BASE) {
        kernel_panic("user: kernel image overlaps user image base");
    }

    struct user_address_space *space =
        (struct user_address_space *)kcalloc(1, sizeof(*space));
    if (!space) {
        return NULL;
    }
    space->pml4 = paging_new_table();

    /* 1. Kernel low footprint (supervisor). */
    if (map_supervisor_2m(space->pml4, 0, USER_IMAGE_BASE,
                          PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        user_address_space_destroy(space);
        return NULL;
    }
    /* 2. Framebuffer + LAPIC MMIO (supervisor, uncached). */
    uint64_t mmio = PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE | PAGE_WRITE_THROUGH;
    if (bi->framebuffer.base && bi->framebuffer.size) {
        if (map_supervisor_2m(space->pml4, bi->framebuffer.base,
                              bi->framebuffer.size, mmio) != 0) {
            user_address_space_destroy(space);
            return NULL;
        }
    }
    if (map_supervisor_2m(space->pml4, lapic_physical_base(), LARGE_PAGE_SIZE, mmio) != 0) {
        user_address_space_destroy(space);
        return NULL;
    }
    /* 2b. Initramfs RAM (supervisor) so ramfs reads work under this CR3. */
    if (bi->initramfs_base && bi->initramfs_size) {
        if (map_supervisor_2m(space->pml4, bi->initramfs_base,
                              bi->initramfs_size, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            user_address_space_destroy(space);
            return NULL;
        }
    }

    /* 3. Allow ring-3 traversal of the intermediate levels that lead to the
     * user image (the 2 MiB leaves below remain supervisor-only). */
    uint64_t *pml4 = phys_table(space->pml4);
    pml4[idx_pml4(USER_IMAGE_BASE)] |= PAGE_USER;
    uint64_t *pdpt = phys_table(pml4[idx_pml4(USER_IMAGE_BASE)] & PAGE_ADDR_MASK);
    pdpt[idx_pdpt(USER_IMAGE_BASE)] |= PAGE_USER;

    return space;
}

uint64_t user_address_space_cr3(struct user_address_space *space) {
    return space ? space->pml4 : 0;
}

int user_map_page(struct user_address_space *space, uint64_t va, uint64_t pa, uint64_t flags) {
    if (!space || va >= USER_TOP || (va & PAGE_MASK) || (pa & PAGE_MASK)) {
        return -1;
    }
    uint64_t leaf = (flags & PAGE_WRITABLE) | PAGE_USER | PAGE_PRESENT;
    return paging_map_4k(space->pml4, va, pa, leaf) == K_OK ? 0 : -1;
}

int user_map_range(struct user_address_space *space, uint64_t va, uint64_t size, uint64_t flags) {
    if (!space || (va & PAGE_MASK)) {
        return -1;
    }
    uint64_t end = va + PAGE_ALIGN_UP(size);
    if (end <= va || end > USER_TOP) {
        return -1;
    }
    for (uint64_t a = va; a < end; a += PAGE_SIZE) {
        uint64_t pa = pmm_alloc_page();
        if (pa == PMM_INVALID_PAGE) {
            return -1;
        }
        memset((void *)(uintptr_t)pa, 0, PAGE_SIZE);   /* fresh user memory is zeroed */
        if (user_map_page(space, a, pa, flags) != 0) {
            pmm_free_page(pa);
            return -1;
        }
    }
    return 0;
}

int user_unmap_range(struct user_address_space *space, uint64_t va, uint64_t size) {
    if (!space || (va & PAGE_MASK)) {
        return -1;
    }
    uint64_t end = va + PAGE_ALIGN_UP(size);
    if (end <= va || end > USER_TOP) {
        return -1;
    }
    for (uint64_t a = va; a < end; a += PAGE_SIZE) {
        uint64_t phys = paging_translate(space->pml4, a);
        if (phys != PAGING_NO_MAP) {
            paging_unmap_4k(space->pml4, a);
            pmm_free_page(phys & PAGE_ADDR_MASK);
        }
    }
    return 0;
}

int user_copy_to_space(struct user_address_space *space, uint64_t user_dst,
                       const void *kernel_src, uint64_t size) {
    if (!space) {
        return -1;
    }
    const uint8_t *src = (const uint8_t *)kernel_src;
    uint64_t cur = user_dst;
    uint64_t remaining = size;
    while (remaining) {
        uint64_t phys = paging_translate(space->pml4, cur);
        if (phys == PAGING_NO_MAP) {
            return -1;
        }
        uint64_t off = cur & PAGE_MASK;
        uint64_t n = PAGE_SIZE - off;
        if (n > remaining) {
            n = remaining;
        }
        memcpy((void *)(uintptr_t)phys, src, n);
        src += n;
        cur += n;
        remaining -= n;
    }
    return 0;
}

int user_copy_from_space(struct user_address_space *space, void *kernel_dst,
                         uint64_t user_src, uint64_t size) {
    if (!space) {
        return -1;
    }
    uint8_t *dst = (uint8_t *)kernel_dst;
    uint64_t cur = user_src;
    uint64_t remaining = size;
    while (remaining) {
        uint64_t phys = paging_translate(space->pml4, cur);
        if (phys == PAGING_NO_MAP) {
            return -1;
        }
        uint64_t off = cur & PAGE_MASK;
        uint64_t n = PAGE_SIZE - off;
        if (n > remaining) {
            n = remaining;
        }
        memcpy(dst, (const void *)(uintptr_t)phys, n);
        dst += n;
        cur += n;
        remaining -= n;
    }
    return 0;
}

int user_zero_in_space(struct user_address_space *space, uint64_t user_dst, uint64_t size) {
    if (!space) {
        return -1;
    }
    uint64_t cur = user_dst;
    uint64_t remaining = size;
    while (remaining) {
        uint64_t phys = paging_translate(space->pml4, cur);
        if (phys == PAGING_NO_MAP) {
            return -1;
        }
        uint64_t off = cur & PAGE_MASK;
        uint64_t n = PAGE_SIZE - off;
        if (n > remaining) {
            n = remaining;
        }
        memset((void *)(uintptr_t)phys, 0, n);
        cur += n;
        remaining -= n;
    }
    return 0;
}

int user_range_is_valid(struct user_address_space *space, uint64_t addr,
                        uint64_t size, int require_write) {
    if (!space) {
        return 0;
    }
    if (size == 0) {
        return 1;
    }
    uint64_t end = addr + size;
    if (end <= addr || end > USER_TOP) {     /* overflow or out of user range */
        return 0;
    }
    for (uint64_t page = PAGE_ALIGN_DOWN(addr); page < end; page += PAGE_SIZE) {
        int is_2m = 0;
        uint64_t leaf = walk_leaf(space->pml4, page, &is_2m);
        if (!(leaf & PAGE_PRESENT) || !(leaf & PAGE_USER)) {
            return 0;
        }
        if (require_write && !(leaf & PAGE_WRITABLE)) {
            return 0;
        }
    }
    return 1;
}

/* Free user leaf frames (PAGE_USER) and every table page we allocated; never
 * free the kernel-shared supervisor leaves (footprint / framebuffer / LAPIC). */
void user_address_space_destroy(struct user_address_space *space) {
    if (!space || !space->pml4) {
        if (space) {
            kfree(space);
        }
        return;
    }
    uint64_t *pml4 = phys_table(space->pml4);
    for (int i = 0; i < 512; i++) {
        if (!(pml4[i] & PAGE_PRESENT)) continue;
        uint64_t *pdpt = phys_table(pml4[i] & PAGE_ADDR_MASK);
        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & PAGE_PRESENT) || (pdpt[j] & PAGE_HUGE)) continue;
            uint64_t *pd = phys_table(pdpt[j] & PAGE_ADDR_MASK);
            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & PAGE_PRESENT) || (pd[k] & PAGE_HUGE)) continue;
                uint64_t *pt = phys_table(pd[k] & PAGE_ADDR_MASK);
                for (int l = 0; l < 512; l++) {
                    if ((pt[l] & PAGE_PRESENT) && (pt[l] & PAGE_USER)) {
                        pmm_free_page(pt[l] & PAGE_ADDR_MASK);
                    }
                }
                pmm_free_page(pd[k] & PAGE_ADDR_MASK);   /* the PT page */
            }
            pmm_free_page(pdpt[j] & PAGE_ADDR_MASK);      /* the PD page */
        }
        pmm_free_page(pml4[i] & PAGE_ADDR_MASK);          /* the PDPT page */
    }
    pmm_free_page(space->pml4);
    kfree(space);
}
