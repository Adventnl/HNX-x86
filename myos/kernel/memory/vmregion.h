/* Virtual memory region tracker.
 *
 * A vm_map is an ordered set of non-overlapping [start, end) regions, each with
 * protection bits and flags. It is the bookkeeping layer that records what a
 * kernel or user address space *intends* to contain; the page tables (vmm) are
 * the hardware realization. This is the foundation for mmap/munmap, demand
 * paging, copy-on-write and fault accounting.
 */
#ifndef MYOS_MEMORY_VMREGION_H
#define MYOS_MEMORY_VMREGION_H

#include "types.h"
#include "list.h"

/* Protection bits. */
#define VM_PROT_READ   0x1u
#define VM_PROT_WRITE  0x2u
#define VM_PROT_EXEC   0x4u

/* Region flags. */
#define VM_FLAG_USER       0x01u   /* user-accessible */
#define VM_FLAG_ANON       0x02u   /* anonymous (zero-fill) */
#define VM_FLAG_FILE       0x04u   /* file-backed */
#define VM_FLAG_COW        0x08u   /* copy-on-write */
#define VM_FLAG_GROWSDOWN  0x10u   /* stack-like */
#define VM_FLAG_FIXED      0x20u   /* placed at an exact address */
#define VM_FLAG_SHARED     0x40u

struct vm_region {
    struct list_node link;
    uint64_t  start;       /* inclusive, page-aligned */
    uint64_t  end;         /* exclusive, page-aligned */
    uint32_t  prot;
    uint32_t  flags;
    const char *name;
    uint64_t  fault_count; /* page faults serviced in this region */
};

struct vm_map {
    struct list_node regions;   /* ordered ascending by start */
    uint64_t  base;             /* lowest mappable address */
    uint64_t  limit;            /* one past highest mappable address */
    size_t    region_count;
    uint64_t  total_mapped;     /* sum of region sizes, bytes */
    const char *name;

    /* Fault accounting. */
    uint64_t  minor_faults;
    uint64_t  major_faults;
    uint64_t  cow_faults;
    uint64_t  protection_faults;
};

void vm_map_init(struct vm_map *m, uint64_t base, uint64_t limit, const char *name);
void vm_map_destroy(struct vm_map *m);

/* Insert a region. Returns 0 on success, -1 on overlap / out-of-range / alloc
 * failure. start/end must be page-aligned with start < end. */
int  vm_map_insert(struct vm_map *m, uint64_t start, uint64_t end,
                   uint32_t prot, uint32_t flags, const char *name);

/* Find the region containing addr, or NULL. */
struct vm_region *vm_map_find(struct vm_map *m, uint64_t addr);

/* Does [start,end) overlap any existing region? */
int  vm_map_overlaps(struct vm_map *m, uint64_t start, uint64_t end);

/* Find a free gap of `size` bytes (page-multiple) at or above `hint`.
 * Returns the chosen start address or 0 if none. */
uint64_t vm_map_find_free(struct vm_map *m, uint64_t hint, uint64_t size);

/* Remove the range [start,end), splitting/truncating regions as needed.
 * Returns the number of bytes unmapped. */
uint64_t vm_map_remove(struct vm_map *m, uint64_t start, uint64_t end);

/* Change protection on [start,end) (must lie within existing regions). */
int  vm_map_protect(struct vm_map *m, uint64_t start, uint64_t end, uint32_t prot);

/* Mark a range copy-on-write (e.g. on fork). Returns regions touched. */
int  vm_map_mark_cow(struct vm_map *m, uint64_t start, uint64_t end);

/* Record a fault for accounting; returns the region or NULL. */
struct vm_region *vm_map_account_fault(struct vm_map *m, uint64_t addr,
                                       int write, int present);

void vm_map_dump(struct vm_map *m);

#endif /* MYOS_MEMORY_VMREGION_H */
