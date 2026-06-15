/* VM region tracker implementation (see kernel/memory/vmregion.h). */
#include "vmregion.h"
#include "slab.h"
#include "memory_layout.h"
#include "log.h"
#include "fmt.h"

static struct kmem_cache *g_region_cache;

static struct vm_region *region_alloc(void) {
    if (!g_region_cache) {
        g_region_cache = kmem_cache_create("vm_region", sizeof(struct vm_region),
                                           16, KMEM_CACHE_ZERO);
        if (!g_region_cache) {
            return NULL;
        }
    }
    return (struct vm_region *)kmem_cache_alloc(g_region_cache);
}

static void region_free(struct vm_region *r) {
    if (g_region_cache && r) {
        kmem_cache_free(g_region_cache, r);
    }
}

void vm_map_init(struct vm_map *m, uint64_t base, uint64_t limit, const char *name) {
    list_init(&m->regions);
    m->base = base;
    m->limit = limit;
    m->region_count = 0;
    m->total_mapped = 0;
    m->name = name;
    m->minor_faults = 0;
    m->major_faults = 0;
    m->cow_faults = 0;
    m->protection_faults = 0;
}

void vm_map_destroy(struct vm_map *m) {
    struct list_node *p, *tmp;
    list_for_each_safe(p, tmp, &m->regions) {
        struct vm_region *r = list_entry(p, struct vm_region, link);
        list_del_init(p);
        region_free(r);
    }
    m->region_count = 0;
    m->total_mapped = 0;
}

int vm_map_overlaps(struct vm_map *m, uint64_t start, uint64_t end) {
    struct vm_region *r;
    list_for_each_entry(r, &m->regions, link) {
        if (start < r->end && r->start < end) {
            return 1;
        }
    }
    return 0;
}

static int aligned_range(uint64_t start, uint64_t end) {
    return (start & PAGE_MASK) == 0 && (end & PAGE_MASK) == 0 && start < end;
}

int vm_map_insert(struct vm_map *m, uint64_t start, uint64_t end,
                  uint32_t prot, uint32_t flags, const char *name) {
    if (!aligned_range(start, end)) {
        return -1;
    }
    if (start < m->base || end > m->limit) {
        return -1;
    }
    if (vm_map_overlaps(m, start, end)) {
        return -1;
    }
    struct vm_region *nr = region_alloc();
    if (!nr) {
        return -1;
    }
    nr->start = start;
    nr->end = end;
    nr->prot = prot;
    nr->flags = flags;
    nr->name = name;
    nr->fault_count = 0;
    list_init(&nr->link);

    /* Insert in ascending order. */
    struct vm_region *r;
    struct list_node *pos = &m->regions;
    list_for_each_entry(r, &m->regions, link) {
        if (r->start > start) {
            pos = &r->link;
            break;
        }
    }
    /* Insert before pos (or at tail if pos stayed at head sentinel). */
    if (pos == &m->regions) {
        list_add_tail(&nr->link, &m->regions);
    } else {
        __list_add(&nr->link, pos->prev, pos);
    }
    m->region_count++;
    m->total_mapped += (end - start);
    return 0;
}

struct vm_region *vm_map_find(struct vm_map *m, uint64_t addr) {
    struct vm_region *r;
    list_for_each_entry(r, &m->regions, link) {
        if (addr >= r->start && addr < r->end) {
            return r;
        }
        if (r->start > addr) {
            break;  /* ordered: no later region can contain addr */
        }
    }
    return NULL;
}

uint64_t vm_map_find_free(struct vm_map *m, uint64_t hint, uint64_t size) {
    size = PAGE_ALIGN_UP(size);
    if (size == 0) {
        return 0;
    }
    uint64_t cursor = hint < m->base ? m->base : PAGE_ALIGN_UP(hint);
    struct vm_region *r;
    list_for_each_entry(r, &m->regions, link) {
        if (r->end <= cursor) {
            continue;
        }
        if (r->start >= cursor + size) {
            /* Gap [cursor, r->start) is large enough. */
            return cursor;
        }
        /* Overlap: jump past this region. */
        if (r->end > cursor) {
            cursor = r->end;
        }
    }
    if (cursor + size <= m->limit) {
        return cursor;
    }
    return 0;
}

uint64_t vm_map_remove(struct vm_map *m, uint64_t start, uint64_t end) {
    if (!aligned_range(start, end)) {
        return 0;
    }
    uint64_t removed = 0;
    struct list_node *p, *tmp;
    list_for_each_safe(p, tmp, &m->regions) {
        struct vm_region *r = list_entry(p, struct vm_region, link);
        if (end <= r->start || start >= r->end) {
            continue;   /* no overlap */
        }
        uint64_t ostart = r->start > start ? r->start : start;
        uint64_t oend = r->end < end ? r->end : end;
        removed += (oend - ostart);

        if (start <= r->start && end >= r->end) {
            /* Whole region removed. */
            m->total_mapped -= (r->end - r->start);
            list_del_init(p);
            region_free(r);
            m->region_count--;
        } else if (start > r->start && end < r->end) {
            /* Punch a hole: shrink r to the left part, add a right part. */
            uint64_t right_start = end;
            uint64_t right_end = r->end;
            m->total_mapped -= (r->end - r->start);
            r->end = start;
            m->total_mapped += (r->end - r->start);
            vm_map_insert(m, right_start, right_end, r->prot, r->flags, r->name);
        } else if (start <= r->start) {
            /* Trim the front. */
            m->total_mapped -= (end - r->start);
            r->start = end;
        } else {
            /* Trim the tail. */
            m->total_mapped -= (r->end - start);
            r->end = start;
        }
    }
    return removed;
}

int vm_map_protect(struct vm_map *m, uint64_t start, uint64_t end, uint32_t prot) {
    if (!aligned_range(start, end)) {
        return -1;
    }
    int touched = 0;
    struct vm_region *r;
    list_for_each_entry(r, &m->regions, link) {
        if (end <= r->start || start >= r->end) {
            continue;
        }
        /* For simplicity, protection applies to whole overlapped regions. */
        r->prot = prot;
        touched++;
    }
    return touched ? 0 : -1;
}

int vm_map_mark_cow(struct vm_map *m, uint64_t start, uint64_t end) {
    int touched = 0;
    struct vm_region *r;
    list_for_each_entry(r, &m->regions, link) {
        if (end <= r->start || start >= r->end) {
            continue;
        }
        if (r->prot & VM_PROT_WRITE) {
            r->flags |= VM_FLAG_COW;
            /* Writable pages become read-only until the COW fault copies. */
        }
        touched++;
    }
    return touched;
}

struct vm_region *vm_map_account_fault(struct vm_map *m, uint64_t addr,
                                       int write, int present) {
    struct vm_region *r = vm_map_find(m, addr);
    if (!r) {
        m->major_faults++;
        return NULL;
    }
    r->fault_count++;
    if (write && (r->flags & VM_FLAG_COW)) {
        m->cow_faults++;
    } else if (write && !(r->prot & VM_PROT_WRITE)) {
        m->protection_faults++;
    } else if (present) {
        m->minor_faults++;
    } else {
        m->major_faults++;
    }
    return r;
}

void vm_map_dump(struct vm_map *m) {
    kdprintf("vm_map '%s' regions=%u mapped=%uKiB faults(min/maj/cow/prot)=%u/%u/%u/%u\n",
             m->name ? m->name : "?", (unsigned)m->region_count,
             (unsigned)(m->total_mapped / 1024),
             (unsigned)m->minor_faults, (unsigned)m->major_faults,
             (unsigned)m->cow_faults, (unsigned)m->protection_faults);
    struct vm_region *r;
    list_for_each_entry(r, &m->regions, link) {
        kdprintf("  [%p-%p) prot=%c%c%c flags=0x%x %s\n",
                 (void *)r->start, (void *)r->end,
                 (r->prot & VM_PROT_READ) ? 'r' : '-',
                 (r->prot & VM_PROT_WRITE) ? 'w' : '-',
                 (r->prot & VM_PROT_EXEC) ? 'x' : '-',
                 r->flags, r->name ? r->name : "");
    }
}
