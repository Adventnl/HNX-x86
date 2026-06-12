/* Kernel virtual memory constants and address helpers. */
#ifndef MYOS_MEMORY_LAYOUT_H
#define MYOS_MEMORY_LAYOUT_H

#include "types.h"

#define PAGE_SIZE 4096ULL
#define PAGE_MASK (PAGE_SIZE - 1ULL)
#define PAGE_SHIFT 12ULL

/* 2 MiB large page (used for the identity map). */
#define LARGE_PAGE_SIZE 0x200000ULL
#define LARGE_PAGE_MASK (LARGE_PAGE_SIZE - 1ULL)

/* Physical link/load address of the kernel image (matches linker.ld). */
#define KERNEL_PHYSICAL_BASE 0x100000ULL

/* All physical memory below 1 MiB is reserved (real-mode IVT/BDA, EBDA, video
 * memory, option ROMs, the legacy BIOS area, SMP trampoline space, etc.) and
 * is never handed out by the page allocator. */
#define LOW_MEMORY_RESERVED_END 0x100000ULL

/* Virtual layout (canonical x86-64 higher half). */
#define KERNEL_HIGHER_HALF_BASE 0xFFFFFFFF80000000ULL
#define KERNEL_DIRECT_MAP_BASE  0xFFFF800000000000ULL
#define KERNEL_HEAP_BASE        0xFFFF900000000000ULL
#define KERNEL_HEAP_SIZE        0x0000000010000000ULL  /* 256 MiB reserved */

#define PAGE_ALIGN_DOWN(x) ((u64)(x) & ~PAGE_MASK)
#define PAGE_ALIGN_UP(x)   (((u64)(x) + PAGE_MASK) & ~PAGE_MASK)
#define LARGE_ALIGN_DOWN(x) ((u64)(x) & ~LARGE_PAGE_MASK)
#define LARGE_ALIGN_UP(x)   (((u64)(x) + LARGE_PAGE_MASK) & ~LARGE_PAGE_MASK)

/* Higher-half / direct-map conversions (valid once the kernel page tables
 * are active; the identity map keeps physical == virtual for low memory). */
#define PHYS_TO_HIGHER_HALF(p) ((u64)(p) + KERNEL_HIGHER_HALF_BASE)
#define PHYS_TO_DIRECT_MAP(p)  ((u64)(p) + KERNEL_DIRECT_MAP_BASE)

#endif /* MYOS_MEMORY_LAYOUT_H */
