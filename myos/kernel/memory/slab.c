/* Slab + size-class allocator implementation (see kernel/memory/slab.h). */
#include "slab.h"
#include "pmm.h"
#include "memory_layout.h"
#include "heap.h"
#include "log.h"
#include "fmt.h"
#include "string.h"
#include "kmath.h"

#define SLAB_MIN_OBJS   8
#define REDZONE_VALUE   0xA5A5A5A5A5A5A5A5ULL
#define KMEM_MAGIC      0x4B4D454Du     /* 'KMEM' */

/* A slab is a contiguous run of pages: [struct kmem_slab][obj][obj]... */
struct kmem_slab {
    struct kmem_cache *cache;
    struct kmem_slab  *next;
    uint8_t           *base;      /* first object */
    size_t             pages;
    size_t             nobjs;
    size_t             inuse;
};

/* A free slot threads a next-pointer through its own storage. */
struct free_slot {
    struct free_slot *next;
};

static struct kmem_cache *g_cache_registry;

/* ---- kmem_cache ----------------------------------------------------------- */

static size_t cache_slot_size(size_t obj_size, size_t align, unsigned flags) {
    size_t s = obj_size;
    if (flags & KMEM_CACHE_REDZONE) {
        s += sizeof(uint64_t);     /* trailing canary */
    }
    if (s < sizeof(struct free_slot)) {
        s = sizeof(struct free_slot);   /* must hold the free-list pointer */
    }
    return align_up_u64(s, align);
}

struct kmem_cache *kmem_cache_create(const char *name, size_t obj_size,
                                     size_t align, unsigned flags) {
    if (align == 0) {
        align = 16;
    }
    if (!is_pow2_u64(align)) {
        align = 16;
    }
    struct kmem_cache *c = (struct kmem_cache *)kmalloc(sizeof(struct kmem_cache));
    if (!c) {
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    c->name = name;
    c->obj_size = obj_size;
    c->align = align;
    c->flags = flags;
    c->slot_size = cache_slot_size(obj_size, align, flags);

    /* Pick a slab size that holds at least SLAB_MIN_OBJS objects. */
    size_t want = sizeof(struct kmem_slab) + c->slot_size * SLAB_MIN_OBJS;
    c->slab_pages = div_round_up_u64(want, PAGE_SIZE);
    if (c->slab_pages == 0) {
        c->slab_pages = 1;
    }
    size_t usable = c->slab_pages * PAGE_SIZE - sizeof(struct kmem_slab);
    c->objs_per_slab = usable / c->slot_size;

    c->next = g_cache_registry;
    g_cache_registry = c;
    return c;
}

static int cache_grow(struct kmem_cache *c) {
    uint64_t phys = pmm_alloc_contig(c->slab_pages);
    if (phys == PMM_INVALID_PAGE) {
        return -1;
    }
    uint8_t *mem = (uint8_t *)(uintptr_t)phys;
    struct kmem_slab *slab = (struct kmem_slab *)mem;
    slab->cache = c;
    slab->base = mem + sizeof(struct kmem_slab);
    slab->pages = c->slab_pages;
    slab->nobjs = c->objs_per_slab;
    slab->inuse = 0;
    slab->next = c->slabs;
    c->slabs = slab;

    /* Thread every object onto the free list. */
    for (size_t i = 0; i < c->objs_per_slab; i++) {
        struct free_slot *fs = (struct free_slot *)(slab->base + i * c->slot_size);
        fs->next = (struct free_slot *)c->free_list;
        c->free_list = fs;
    }
    c->slab_count++;
    c->grow_count++;
    return 0;
}

static void set_redzone(struct kmem_cache *c, void *obj) {
    if (c->flags & KMEM_CACHE_REDZONE) {
        uint64_t *rz = (uint64_t *)((uint8_t *)obj + c->obj_size);
        *rz = REDZONE_VALUE;
    }
}

static int check_redzone(struct kmem_cache *c, void *obj) {
    if (c->flags & KMEM_CACHE_REDZONE) {
        uint64_t *rz = (uint64_t *)((uint8_t *)obj + c->obj_size);
        return *rz == REDZONE_VALUE;
    }
    return 1;
}

void *kmem_cache_alloc(struct kmem_cache *c) {
    if (!c->free_list) {
        if (cache_grow(c) != 0) {
            return NULL;
        }
    }
    struct free_slot *fs = (struct free_slot *)c->free_list;
    c->free_list = fs->next;
    void *obj = (void *)fs;

    c->alloc_count++;
    c->active = c->alloc_count - c->free_count;
    if (c->flags & KMEM_CACHE_ZERO) {
        memset(obj, 0, c->obj_size);
    }
    set_redzone(c, obj);
    return obj;
}

void kmem_cache_free(struct kmem_cache *c, void *obj) {
    if (!obj) {
        return;
    }
    if (!check_redzone(c, obj)) {
        kernel_log_error("slab: redzone corruption detected on free");
    }
    struct free_slot *fs = (struct free_slot *)obj;
    fs->next = (struct free_slot *)c->free_list;
    c->free_list = fs;
    c->free_count++;
    c->active = c->alloc_count - c->free_count;
}

void kmem_cache_destroy(struct kmem_cache *c) {
    struct kmem_slab *s = c->slabs;
    while (s) {
        struct kmem_slab *next = s->next;
        pmm_free_contig((uint64_t)(uintptr_t)s, s->pages);
        s = next;
    }
    /* Unlink from the registry. */
    struct kmem_cache **pp = &g_cache_registry;
    while (*pp) {
        if (*pp == c) {
            *pp = c->next;
            break;
        }
        pp = &(*pp)->next;
    }
    kfree(c);
}

size_t kmem_cache_active(const struct kmem_cache *c) {
    return c->active;
}

/* ---- Size-class general allocator ----------------------------------------- */

/* Usable sizes per class; the slot also carries an 8-byte header. */
static const size_t g_class_usable[] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};
#define NUM_CLASSES (sizeof(g_class_usable) / sizeof(g_class_usable[0]))
#define KMEM_HDR_SIZE 16u

struct kmem_hdr {
    uint32_t magic;
    uint16_t class_idx;   /* 0xFFFF => large allocation */
    uint16_t flags;
    uint64_t npages;      /* for large allocations */
};

static struct kmem_cache *g_size_caches[NUM_CLASSES];
static uint64_t g_total_allocs;
static uint64_t g_total_frees;
static uint64_t g_bytes_live;
static uint64_t g_large_allocs;

void slab_init(void) {
    for (size_t i = 0; i < NUM_CLASSES; i++) {
        /* Slot = usable + header, 16-byte aligned. */
        char namebuf[24];
        ksnprintf(namebuf, sizeof(namebuf), "kmem-%u", (unsigned)g_class_usable[i]);
        /* The name string must persist; stash a static copy. */
        static char names[NUM_CLASSES][24];
        strlcpy(names[i], namebuf, sizeof(names[i]));
        g_size_caches[i] = kmem_cache_create(names[i],
                                             g_class_usable[i] + KMEM_HDR_SIZE,
                                             16, 0);
    }
}

static int size_to_class(size_t size) {
    for (size_t i = 0; i < NUM_CLASSES; i++) {
        if (size <= g_class_usable[i]) {
            return (int)i;
        }
    }
    return -1;
}

void *kmem_alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    g_total_allocs++;

    int cls = size_to_class(size);
    if (cls >= 0) {
        void *slot = kmem_cache_alloc(g_size_caches[cls]);
        if (!slot) {
            return NULL;
        }
        struct kmem_hdr *h = (struct kmem_hdr *)slot;
        h->magic = KMEM_MAGIC;
        h->class_idx = (uint16_t)cls;
        h->flags = 0;
        h->npages = 0;
        g_bytes_live += g_class_usable[cls];
        return (uint8_t *)slot + KMEM_HDR_SIZE;
    }

    /* Large allocation: round up to whole pages, header at the base. */
    size_t total;
    if (check_add_size(size, KMEM_HDR_SIZE, &total) != 0) {
        return NULL;
    }
    size_t npages = div_round_up_u64(total, PAGE_SIZE);
    uint64_t phys = pmm_alloc_contig(npages);
    if (phys == PMM_INVALID_PAGE) {
        return NULL;
    }
    struct kmem_hdr *h = (struct kmem_hdr *)(uintptr_t)phys;
    h->magic = KMEM_MAGIC;
    h->class_idx = 0xFFFF;
    h->flags = 0;
    h->npages = npages;
    g_large_allocs++;
    g_bytes_live += npages * PAGE_SIZE;
    return (uint8_t *)h + KMEM_HDR_SIZE;
}

void *kmem_zalloc(size_t size) {
    void *p = kmem_alloc(size);
    if (p) {
        memset(p, 0, size);
    }
    return p;
}

size_t kmem_alloc_size(const void *ptr) {
    if (!ptr) {
        return 0;
    }
    const struct kmem_hdr *h = (const struct kmem_hdr *)((const uint8_t *)ptr - KMEM_HDR_SIZE);
    if (h->magic != KMEM_MAGIC) {
        return 0;
    }
    if (h->class_idx == 0xFFFF) {
        return h->npages * PAGE_SIZE - KMEM_HDR_SIZE;
    }
    return g_class_usable[h->class_idx];
}

void kmem_free(void *ptr) {
    if (!ptr) {
        return;
    }
    struct kmem_hdr *h = (struct kmem_hdr *)((uint8_t *)ptr - KMEM_HDR_SIZE);
    if (h->magic != KMEM_MAGIC) {
        kernel_log_error("kmem_free: bad/double free (magic mismatch)");
        return;
    }
    g_total_frees++;
    if (h->class_idx == 0xFFFF) {
        uint64_t npages = h->npages;
        h->magic = 0;
        if (g_bytes_live >= npages * PAGE_SIZE) {
            g_bytes_live -= npages * PAGE_SIZE;
        }
        pmm_free_contig((uint64_t)(uintptr_t)h, npages);
        return;
    }
    uint16_t cls = h->class_idx;
    h->magic = 0;     /* poison so double-free is detected */
    if (g_bytes_live >= g_class_usable[cls]) {
        g_bytes_live -= g_class_usable[cls];
    }
    kmem_cache_free(g_size_caches[cls], h);
}

void *kmem_realloc(void *ptr, size_t new_size) {
    if (!ptr) {
        return kmem_alloc(new_size);
    }
    if (new_size == 0) {
        kmem_free(ptr);
        return NULL;
    }
    size_t old = kmem_alloc_size(ptr);
    if (old >= new_size) {
        return ptr;     /* shrink in place */
    }
    void *n = kmem_alloc(new_size);
    if (!n) {
        return NULL;
    }
    memcpy(n, ptr, old);
    kmem_free(ptr);
    return n;
}

void kmem_get_stats(struct kmem_stats *out) {
    out->total_allocs = g_total_allocs;
    out->total_frees = g_total_frees;
    out->live_objects = g_total_allocs - g_total_frees;
    out->bytes_live = g_bytes_live;
    out->large_allocs = g_large_allocs;
    uint64_t n = 0;
    for (struct kmem_cache *c = g_cache_registry; c; c = c->next) {
        n++;
    }
    out->cache_count = n;
}

void kmem_dump_caches(void) {
    kernel_log_line("kmem caches:");
    for (struct kmem_cache *c = g_cache_registry; c; c = c->next) {
        kdprintf("  %s: obj=%u slot=%u active=%u slabs=%u\n",
                 c->name, (unsigned)c->obj_size, (unsigned)c->slot_size,
                 (unsigned)c->active, (unsigned)c->slab_count);
    }
}

void kmem_leak_dump(void) {
    int leaks = 0;
    for (struct kmem_cache *c = g_cache_registry; c; c = c->next) {
        if (c->active != 0) {
            kdprintf("  LEAK %s: %u objects still active\n",
                     c->name, (unsigned)c->active);
            leaks++;
        }
    }
    if (!leaks) {
        kernel_log_line("  (no cache leaks)");
    }
}
