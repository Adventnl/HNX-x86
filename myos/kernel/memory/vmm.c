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

int vmm_map_page(uint64_t va, uint64_t pa, uint64_t flags) {
    return paging_map_4k(g_pml4, va, pa, flags | PAGE_PRESENT) == K_OK ? 0 : -1;
}

int vmm_unmap_page(uint64_t va) {
    return paging_unmap_4k(g_pml4, va) == K_OK ? 0 : -1;
}

uint64_t vmm_get_physical(uint64_t va) {
    return paging_translate(g_pml4, va);
}
