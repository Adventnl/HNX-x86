/* Slab allocator + size-class general allocator.
 *
 * Two layers:
 *   1. kmem_cache    — object caches for one fixed object size. Backs the
 *                      hot, frequently allocated kernel structs.
 *   2. kmem_alloc/free — a malloc-style allocator routed through a fixed set
 *                      of power-of-two size classes (each its own cache), with
 *                      a large-object fallback straight to the page allocator.
 *
 * Backing memory comes from the physical page allocator (pmm_alloc_contig),
 * whose pages are identity-mapped, so the returned addresses are usable
 * pointers directly. Every allocation carries statistics and (in redzone debug
 * builds) canaries that are validated on free.
 */
#ifndef MYOS_MEMORY_SLAB_H
#define MYOS_MEMORY_SLAB_H

#include "types.h"

/* ---- kmem_cache ----------------------------------------------------------- */

struct kmem_slab;

struct kmem_cache {
    const char       *name;
    size_t            obj_size;     /* requested object size */
    size_t            slot_size;    /* aligned slot incl. bookkeeping */
    size_t            align;
    unsigned          flags;
    void             *free_list;    /* singly-linked free slots */
    struct kmem_slab *slabs;        /* all slabs owned by this cache */
    size_t            objs_per_slab;
    size_t            slab_pages;

    /* statistics */
    uint64_t          alloc_count;
    uint64_t          free_count;
    uint64_t          active;       /* alloc_count - free_count */
    uint64_t          slab_count;
    uint64_t          grow_count;

    struct kmem_cache *next;        /* global cache registry link */
};

#define KMEM_CACHE_REDZONE  0x1u    /* add/validate canaries (debug) */
#define KMEM_CACHE_ZERO     0x2u    /* zero objects on alloc */

struct kmem_cache *kmem_cache_create(const char *name, size_t obj_size,
                                     size_t align, unsigned flags);
void  *kmem_cache_alloc(struct kmem_cache *c);
void   kmem_cache_free(struct kmem_cache *c, void *obj);
void   kmem_cache_destroy(struct kmem_cache *c);
size_t kmem_cache_active(const struct kmem_cache *c);

/* ---- General allocator ---------------------------------------------------- */

void  slab_init(void);              /* build the size-class caches */
void *kmem_alloc(size_t size);
void *kmem_zalloc(size_t size);
void *kmem_realloc(void *ptr, size_t new_size);
void  kmem_free(void *ptr);
size_t kmem_alloc_size(const void *ptr);   /* usable size of an allocation */

/* ---- Diagnostics ---------------------------------------------------------- */

struct kmem_stats {
    uint64_t total_allocs;
    uint64_t total_frees;
    uint64_t live_objects;
    uint64_t bytes_live;
    uint64_t large_allocs;
    uint64_t cache_count;
};
void kmem_get_stats(struct kmem_stats *out);
void kmem_dump_caches(void);
/* Print a one-line summary of any cache whose active count is non-zero. */
void kmem_leak_dump(void);

#endif /* MYOS_MEMORY_SLAB_H */
