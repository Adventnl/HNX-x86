/* Virtual memory manager: build and activate kernel-owned page tables.
 *
 * Strategy for Prompt 2:
 *   - Identity-map all physical RAM + the framebuffer with 2 MiB pages. This
 *     single mapping satisfies every required mapping (kernel code/data/bss,
 *     kernel stack, framebuffer, boot_info, UEFI map, PMM bitmap) and the low
 *     identity mapping needed for the running code, so loading our CR3 is safe.
 *   - Also map the kernel image into the higher half for future use.
 */
#include "vmm.h"
#include "paging.h"
#include "pmm.h"
#include "memory_layout.h"
#include "log.h"
#include "cpu.h"

#define VMM_SCRATCH_VA 0xFFFFFFFFC0000000ULL

static uint64_t g_pml4;
static int g_cr3_loaded;

/* Largest physical address we must keep addressable. */
static uint64_t compute_identity_top(const struct boot_info *bi) {
    uint64_t top = 0x100000000ULL;   /* always cover the low 4 GiB */

    uint64_t base = bi->memory_map.map_base;
    uint64_t stride = bi->memory_map.descriptor_size;
    uint64_t count = bi->memory_map.map_size / stride;
    for (uint64_t i = 0; i < count; i++) {
        const struct efi_memory_descriptor_kernel *d =
            (const struct efi_memory_descriptor_kernel *)(uintptr_t)(base + i * stride);
        if (!efi_type_is_ram(d->type)) {
            continue;   /* the framebuffer (MMIO) is added explicitly below */
        }
        uint64_t end = d->physical_start + d->number_of_pages * PAGE_SIZE;
        if (end > top) {
            top = end;
        }
    }

    uint64_t fb_end = bi->framebuffer.base + bi->framebuffer.size;
    if (fb_end > top) {
        top = fb_end;
    }
    return LARGE_ALIGN_UP(top);
}

void vmm_init(const struct boot_info *bi) {
    g_pml4 = paging_new_table();

    uint64_t identity_top = compute_identity_top(bi);
    uint64_t fb_start = LARGE_ALIGN_DOWN(bi->framebuffer.base);
    uint64_t fb_end   = LARGE_ALIGN_UP(bi->framebuffer.base + bi->framebuffer.size);

    /* Identity map [0, identity_top) using 2 MiB pages. */
    for (uint64_t pa = 0; pa < identity_top; pa += LARGE_PAGE_SIZE) {
        uint64_t flags = PAGE_PRESENT | PAGE_WRITABLE;
        if (pa >= fb_start && pa < fb_end) {
            flags |= PAGE_CACHE_DISABLE | PAGE_WRITE_THROUGH;  /* framebuffer MMIO */
        }
        paging_map_2m(g_pml4, pa, pa, flags);
    }

    /* Higher-half kernel image mapping (KERNEL_HIGHER_HALF_BASE + phys). */
    uint64_t kbase = LARGE_ALIGN_DOWN(bi->kernel.kernel_base);
    uint64_t kend  = LARGE_ALIGN_UP(bi->kernel.kernel_base + bi->kernel.kernel_size);
    for (uint64_t pa = kbase; pa < kend; pa += LARGE_PAGE_SIZE) {
        paging_map_2m(g_pml4, KERNEL_HIGHER_HALF_BASE + (pa - kbase), pa,
                      PAGE_PRESENT | PAGE_WRITABLE);
    }
}

void vmm_load_kernel_address_space(void) {
    paging_load_cr3(g_pml4);
    g_cr3_loaded = 1;
}

int vmm_cr3_loaded(void) {
    return g_cr3_loaded;
}

uint64_t vmm_kernel_pml4(void) {
    return g_pml4;
}

int vmm_map_mmio_2m(uint64_t phys) {
    if (phys & LARGE_PAGE_MASK) {
        return -1;
    }
    /* Identity remap with MMIO-safe attributes (overwrites the cached
     * identity-map entry for this 2 MiB region). */
    if (paging_map_2m(g_pml4, phys, phys,
                      PAGE_PRESENT | PAGE_WRITABLE |
                      PAGE_CACHE_DISABLE | PAGE_WRITE_THROUGH) != K_OK) {
        return -1;
    }
    return 0;
}

int vmm_map_page(uint64_t va, uint64_t pa, uint64_t flags) {
    return paging_map_4k(g_pml4, va, pa, flags | PAGE_PRESENT) == K_OK ? 0 : -1;
}

int vmm_unmap_page(uint64_t va) {
    return paging_unmap_4k(g_pml4, va) == K_OK ? 0 : -1;
}

uint64_t vmm_get_physical(uint64_t va) {
    return paging_translate(g_pml4, va);
}

/* Confirm `va` is mapped and (for an identity region) maps back to its own
 * physical address. Logs detail under MYOS_TEST_VERBOSE. */
static int check_identity(const char *label, uint64_t va) {
    uint64_t phys = paging_translate(g_pml4, va);
#ifdef MYOS_TEST_VERBOSE
    kernel_log(label);
    kernel_log_hex64(" va=", va);
    kernel_log_hex64("        -> phys=", phys);
#else
    (void)label;
#endif
    if (phys == PAGING_NO_MAP) {
        return 0;
    }
    return (phys & ~PAGE_MASK) == (va & ~PAGE_MASK);
}

int vmm_validate_required_mappings(const struct boot_info *bi) {
    int ok = 1;

    /* CR3 must point at our PML4 (ignoring the low flag bits). */
    if (g_cr3_loaded) {
        uint64_t cr3 = x86_read_cr3();
        if ((cr3 & ~PAGE_MASK) != g_pml4) {
            kernel_log_error("vmm: CR3 does not match kernel PML4");
            ok = 0;
        }
    }

    /* Required identity-mapped regions. */
    ok &= check_identity("  kernel_entry ", bi->kernel.kernel_entry);
    ok &= check_identity("  kernel_base  ", bi->kernel.kernel_base);
    ok &= check_identity("  framebuffer  ", bi->framebuffer.base);
    ok &= check_identity("  boot_info    ", (uint64_t)(uintptr_t)bi);
    ok &= check_identity("  uefi_map     ", bi->memory_map.map_base);
    ok &= check_identity("  pmm_bitmap   ", pmm_bitmap_base());

    /* Round-trip a freshly mapped scratch page. */
    uint64_t phys = pmm_alloc_page();
    if (phys == PMM_INVALID_PAGE) {
        return 0;
    }
    if (vmm_map_page(VMM_SCRATCH_VA, phys, PAGE_WRITABLE) != 0) {
        pmm_free_page(phys);
        return 0;
    }
    if ((vmm_get_physical(VMM_SCRATCH_VA) & ~PAGE_MASK) != phys) {
        ok = 0;
    }
    if (g_cr3_loaded) {
        volatile uint64_t *p = (volatile uint64_t *)VMM_SCRATCH_VA;
        *p = 0xCAFEF00DDEADBEEFULL;
        if (*p != 0xCAFEF00DDEADBEEFULL) {
            ok = 0;
        }
    }
    vmm_unmap_page(VMM_SCRATCH_VA);
    pmm_free_page(phys);

    return ok;
}
