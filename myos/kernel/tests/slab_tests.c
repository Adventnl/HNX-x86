/* Tests for the kmem_cache slab layer: object caching, free-list reuse, slab
 * growth, statistics and the redzone debug mode. Marker: "slab allocator tests". */
#include "ktest.h"
#include "slab.h"
#include "string.h"

struct widget {
    uint64_t a;
    uint64_t b;
    char     name[16];
};

void slab_tests_run(void) {
    KT_BEGIN();

    struct kmem_cache *c = kmem_cache_create("test-widget", sizeof(struct widget),
                                             16, KMEM_CACHE_ZERO);
    KT_CHECK(c != NULL, "cache create");
    KT_CHECK_EQ(kmem_cache_active(c), 0, "new cache empty");

    struct widget *w0 = (struct widget *)kmem_cache_alloc(c);
    KT_CHECK(w0 != NULL, "cache alloc");
    KT_CHECK(w0->a == 0 && w0->b == 0, "ZERO flag zeroes object");
    KT_CHECK_EQ(kmem_cache_active(c), 1, "active 1");

    w0->a = 0xDEAD;
    strlcpy(w0->name, "w0", sizeof(w0->name));

    /* Free + reuse hands back the same slot. */
    kmem_cache_free(c, w0);
    KT_CHECK_EQ(kmem_cache_active(c), 0, "active 0 after free");
    struct widget *w1 = (struct widget *)kmem_cache_alloc(c);
    KT_CHECK_EQ((uintptr_t)w1, (uintptr_t)w0, "slot reused");

    /* Allocate enough to force at least one slab growth. */
    struct widget *batch[64];
    for (int i = 0; i < 64; i++) {
        batch[i] = (struct widget *)kmem_cache_alloc(c);
        KT_CHECK(batch[i] != NULL, "batch alloc");
        batch[i]->a = (uint64_t)i;
    }
    KT_CHECK(c->slab_count >= 1, "at least one slab");
    KT_CHECK(c->grow_count >= 1, "cache grew");
    KT_CHECK_EQ(kmem_cache_active(c), 65, "active 65 (w1 + 64)");

    /* Content of each batch object is intact (no aliasing). */
    int intact = 1;
    for (int i = 0; i < 64; i++) {
        if (batch[i]->a != (uint64_t)i) {
            intact = 0;
        }
    }
    KT_CHECK(intact, "batch objects not aliased");

    for (int i = 0; i < 64; i++) {
        kmem_cache_free(c, batch[i]);
    }
    kmem_cache_free(c, w1);
    KT_CHECK_EQ(kmem_cache_active(c), 0, "all freed");

    /* Stats sanity. */
    KT_CHECK(c->alloc_count == c->free_count, "alloc/free balanced");

    kmem_cache_destroy(c);

    /* Redzone cache: a correctly-used object frees cleanly. */
    struct kmem_cache *rc = kmem_cache_create("rz", 32, 16, KMEM_CACHE_REDZONE);
    KT_CHECK(rc != NULL, "redzone cache create");
    void *p = kmem_cache_alloc(rc);
    KT_CHECK(p != NULL, "redzone alloc");
    memset(p, 0x42, 32);              /* fill exactly the object, not the canary */
    kmem_cache_free(rc, p);           /* canary intact => no corruption logged */
    KT_CHECK_EQ(kmem_cache_active(rc), 0, "redzone freed clean");
    kmem_cache_destroy(rc);

    KT_RESULT("slab allocator tests");
}
