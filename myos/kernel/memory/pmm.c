/* Physical memory manager: parse the UEFI memory map and manage 4 KiB pages
 * with a bitmap. A set bit means the page is used/reserved.
 *
 * Allocation policy (conservative): only EfiConventionalMemory is ever freed.
 * Everything else — reserved, runtime, ACPI, MMIO, boot-services, the low
 * 1 MiB, the kernel image, boot_info, the UEFI map, and the bitmap itself —
 * stays reserved. */
#include "pmm.h"
#include "memory_layout.h"
#include "log.h"
#include "panic.h"

static uint8_t *g_bitmap;       /* identity-mapped physical address */
static uint64_t g_bitmap_bytes;
static uint64_t g_total_pages;
static uint64_t g_free_pages;
static uint64_t g_reserved_pages;
static uint64_t g_bitmap_phys;
static uint64_t g_bitmap_pages;
static uint64_t g_lowest_page;   /* first allocatable physical address */
static uint64_t g_highest_addr;  /* highest managed RAM address */

static const struct boot_info *g_bi;

static inline void bit_set(uint64_t page) {
    g_bitmap[page >> 3] |= (uint8_t)(1u << (page & 7));
}
static inline void bit_clear(uint64_t page) {
    g_bitmap[page >> 3] &= (uint8_t)~(1u << (page & 7));
}
static inline int bit_test(uint64_t page) {
    return (g_bitmap[page >> 3] >> (page & 7)) & 1;
}

static const struct efi_memory_descriptor_kernel *
desc_at(uint64_t index) {
    uint64_t base = g_bi->memory_map.map_base;
    uint64_t stride = g_bi->memory_map.descriptor_size;
    return (const struct efi_memory_descriptor_kernel *)(uintptr_t)(base + index * stride);
}

static uint64_t desc_count(void) {
    return g_bi->memory_map.map_size / g_bi->memory_map.descriptor_size;
}

/* Mark [start, start+size) pages used (clamped to the bitmap range). */
static void reserve_range(uint64_t start, uint64_t size) {
    if (size == 0) {
        return;
    }
    uint64_t first = PAGE_ALIGN_DOWN(start) >> PAGE_SHIFT;
    uint64_t last  = PAGE_ALIGN_UP(start + size) >> PAGE_SHIFT;
    for (uint64_t p = first; p < last && p < g_total_pages; p++) {
        if (!bit_test(p)) {
            bit_set(p);
            if (g_free_pages) {
                g_free_pages--;
            }
        }
    }
}

void pmm_init(const struct boot_info *boot_info) {
    g_bi = boot_info;

    /* 1. Highest physical RAM address -> bitmap size (MMIO/reserved excluded). */
    uint64_t highest = 0;
    uint64_t n = desc_count();
    for (uint64_t i = 0; i < n; i++) {
        const struct efi_memory_descriptor_kernel *d = desc_at(i);
        if (!efi_type_is_ram(d->type)) {
            continue;   /* don't size the bitmap to high MMIO windows */
        }
        uint64_t end = d->physical_start + d->number_of_pages * PAGE_SIZE;
        if (end > highest) {
            highest = end;
        }
    }
    g_highest_addr = highest;
    g_total_pages  = highest >> PAGE_SHIFT;
    g_bitmap_bytes = (g_total_pages + 7) / 8;
    g_bitmap_pages = PAGE_ALIGN_UP(g_bitmap_bytes) >> PAGE_SHIFT;

    /* 2. Find a conventional region (at/above 1 MiB) to hold the bitmap. */
    g_bitmap_phys = 0;
    for (uint64_t i = 0; i < n; i++) {
        const struct efi_memory_descriptor_kernel *d = desc_at(i);
        if (d->type != EFI_CONVENTIONAL_MEMORY) {
            continue;
        }
        if (d->number_of_pages >= g_bitmap_pages &&
            d->physical_start >= LOW_MEMORY_RESERVED_END) {
            g_bitmap_phys = d->physical_start;
            break;
        }
    }
    if (g_bitmap_phys == 0) {
        kernel_panic("pmm: no region for bitmap");
    }
    g_bitmap = (uint8_t *)(uintptr_t)g_bitmap_phys;

    /* 3. Start with everything used. */
    for (uint64_t b = 0; b < g_bitmap_bytes; b++) {
        g_bitmap[b] = 0xFF;
    }
    g_free_pages = 0;

    /* 4. Free conventional memory only. */
    for (uint64_t i = 0; i < n; i++) {
        const struct efi_memory_descriptor_kernel *d = desc_at(i);
        if (d->type != EFI_CONVENTIONAL_MEMORY) {
            continue;   /* reserved/runtime/ACPI/MMIO/boot-services stay used */
        }
        uint64_t first = d->physical_start >> PAGE_SHIFT;
        uint64_t last  = first + d->number_of_pages;
        for (uint64_t p = first; p < last && p < g_total_pages; p++) {
            if (bit_test(p)) {
                bit_clear(p);
                g_free_pages++;
            }
        }
    }

    /* 5. Re-reserve the things that must never be handed out. */
    reserve_range(0, LOW_MEMORY_RESERVED_END);                       /* low 1 MiB */
    reserve_range(boot_info->kernel.kernel_base,
                  boot_info->kernel.kernel_size);                    /* kernel image */
    reserve_range((uint64_t)(uintptr_t)boot_info, sizeof(struct boot_info)); /* boot_info */
    reserve_range(boot_info->memory_map.map_base,
                  boot_info->memory_map.map_size);                   /* UEFI map */
    reserve_range(boot_info->framebuffer.base,
                  boot_info->framebuffer.size);                      /* framebuffer */
    reserve_range(g_bitmap_phys, g_bitmap_pages * PAGE_SIZE);        /* the bitmap itself */
    /* ACPI reclaim/NVS and UEFI runtime ranges are non-conventional and were
     * never freed in step 4, so they remain reserved automatically. */

    g_reserved_pages = g_total_pages - g_free_pages;

    /* 6. Record the lowest allocatable page for diagnostics. */
    g_lowest_page = 0;
    for (uint64_t p = LOW_MEMORY_RESERVED_END >> PAGE_SHIFT; p < g_total_pages; p++) {
        if (!bit_test(p)) {
            g_lowest_page = p << PAGE_SHIFT;
            break;
        }
    }
}

uint64_t pmm_alloc_page(void) {
    uint64_t start = LOW_MEMORY_RESERVED_END >> PAGE_SHIFT;   /* never below 1 MiB */
    for (uint64_t p = start; p < g_total_pages; p++) {
        if (!bit_test(p)) {
            bit_set(p);
            g_free_pages--;
            return p << PAGE_SHIFT;
        }
    }
    return PMM_INVALID_PAGE;   /* out of memory */
}

uint64_t pmm_alloc_contig(uint64_t count) {
    if (count == 0) {
        return PMM_INVALID_PAGE;
    }
    if (count == 1) {
        return pmm_alloc_page();
    }
    uint64_t start = LOW_MEMORY_RESERVED_END >> PAGE_SHIFT;
    for (uint64_t p = start; p + count <= g_total_pages; p++) {
        uint64_t run = 0;
        while (run < count && !bit_test(p + run)) {
            run++;
        }
        if (run == count) {
            for (uint64_t i = 0; i < count; i++) {
                bit_set(p + i);
            }
            g_free_pages -= count;
            return p << PAGE_SHIFT;
        }
        p += run;   /* skip the used page that broke the run */
    }
    return PMM_INVALID_PAGE;
}

void pmm_free_contig(uint64_t physical_address, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        pmm_free_page(physical_address + i * PAGE_SIZE);
    }
}

void pmm_free_page(uint64_t physical_address) {
    if (physical_address == 0 || (physical_address & PAGE_MASK)) {
        kernel_panic_hex("pmm: invalid free", physical_address);
    }
    if (physical_address < LOW_MEMORY_RESERVED_END) {
        kernel_panic_hex("pmm: free of reserved low memory", physical_address);
    }
    uint64_t page = physical_address >> PAGE_SHIFT;
    if (page >= g_total_pages) {
        kernel_panic_hex("pmm: free out of range", physical_address);
    }
    if (!bit_test(page)) {
        kernel_panic_hex("pmm: double free", physical_address);
    }
    bit_clear(page);
    g_free_pages++;
}

uint64_t pmm_total_pages(void)    { return g_total_pages; }
uint64_t pmm_free_pages(void)     { return g_free_pages; }
uint64_t pmm_used_pages(void)     { return g_total_pages - g_free_pages; }
uint64_t pmm_reserved_pages(void) { return g_reserved_pages; }
uint64_t pmm_bitmap_base(void)    { return g_bitmap_phys; }
uint64_t pmm_bitmap_size(void)    { return g_bitmap_bytes; }
uint64_t pmm_lowest_page(void)    { return g_lowest_page; }
uint64_t pmm_highest_address(void){ return g_highest_addr; }

void pmm_dump_stats(void) {
    kernel_log_hex64("    total pages    : ", g_total_pages);
    kernel_log_hex64("    free  pages    : ", g_free_pages);
    kernel_log_hex64("    used  pages    : ", pmm_used_pages());
    kernel_log_hex64("    reserved pages : ", g_reserved_pages);
    kernel_log_hex64("    bitmap phys    : ", g_bitmap_phys);
    kernel_log_hex64("    bitmap bytes   : ", g_bitmap_bytes);
    kernel_log_hex64("    lowest alloc   : ", g_lowest_page);
    kernel_log_hex64("    highest RAM    : ", g_highest_addr);
}

int pmm_stress_test(int verbose) {
    enum { N = 64 };
    uint64_t pages[N];
    uint64_t before = pmm_free_pages();

    for (int i = 0; i < N; i++) {
        pages[i] = pmm_alloc_page();
        if (pages[i] == PMM_INVALID_PAGE) {
            return 0;                       /* allocation failed */
        }
        if (pages[i] & PAGE_MASK) {
            return 0;                       /* not 4 KiB aligned */
        }
        if (pages[i] < LOW_MEMORY_RESERVED_END) {
            return 0;                       /* below the 1 MiB floor */
        }
        for (int j = 0; j < i; j++) {
            if (pages[j] == pages[i]) {
                return 0;                   /* duplicate */
            }
        }
        if (verbose) {
            kernel_log_hex64("      alloc page : ", pages[i]);
        }
    }

    if (pmm_free_pages() != before - N) {
        return 0;
    }
    for (int i = 0; i < N; i++) {
        pmm_free_page(pages[i]);
    }
    if (pmm_free_pages() != before) {
        return 0;                           /* free count not restored */
    }
    return 1;
}
