/* Tests for the size-class general allocator (kmem_alloc/kmem_free/realloc).
 * Verifies real reclamation: freeing an object and re-allocating the same size
 * must hand the storage back. Marker: "kmalloc/kfree tests". */
#include "ktest.h"
#include "slab.h"
#include "string.h"

void allocator_tests_run(void) {
    KT_BEGIN();

    /* Basic alloc/use/free. */
    uint8_t *a = (uint8_t *)kmem_alloc(64);
    KT_CHECK(a != NULL, "alloc 64");
    for (int i = 0; i < 64; i++) {
        a[i] = (uint8_t)(i ^ 0x5A);
    }
    int ok = 1;
    for (int i = 0; i < 64; i++) {
        if (a[i] != (uint8_t)(i ^ 0x5A)) {
            ok = 0;
        }
    }
    KT_CHECK(ok, "write/read 64 bytes");
    KT_CHECK(kmem_alloc_size(a) >= 64, "usable size >= request");

    /* Real free + reuse: free a, the next same-class alloc should reuse it. */
    kmem_free(a);
    uint8_t *b = (uint8_t *)kmem_alloc(64);
    KT_CHECK_EQ((uintptr_t)b, (uintptr_t)a, "freed slot is reused");
    kmem_free(b);

    /* Many allocations across size classes, all distinct, all writable. */
    void *ptrs[64];
    for (int i = 0; i < 64; i++) {
        size_t sz = 8 + (size_t)i * 13;
        ptrs[i] = kmem_alloc(sz);
        KT_CHECK(ptrs[i] != NULL, "bulk alloc");
        memset(ptrs[i], 0xCC, sz);
    }
    /* No two live allocations alias. */
    int distinct = 1;
    for (int i = 0; i < 64 && distinct; i++) {
        for (int j = i + 1; j < 64; j++) {
            if (ptrs[i] == ptrs[j]) {
                distinct = 0;
                break;
            }
        }
    }
    KT_CHECK(distinct, "all live allocations distinct");
    for (int i = 0; i < 64; i++) {
        kmem_free(ptrs[i]);
    }

    /* zalloc returns zeroed memory. */
    uint32_t *z = (uint32_t *)kmem_zalloc(128);
    KT_CHECK(z != NULL, "zalloc");
    int zeroed = 1;
    for (int i = 0; i < 32; i++) {
        if (z[i] != 0) {
            zeroed = 0;
        }
    }
    KT_CHECK(zeroed, "zalloc zeroes");

    /* realloc grows and preserves content. */
    char *s = (char *)kmem_alloc(16);
    strlcpy(s, "hello", 16);
    s = (char *)kmem_realloc(s, 256);
    KT_CHECK(s != NULL, "realloc grow");
    KT_CHECK(strcmp(s, "hello") == 0, "realloc preserves data");
    kmem_free(s);
    kmem_free(z);

    /* Large allocation (> largest size class) goes to the page path. */
    void *big = kmem_alloc(64 * 1024);
    KT_CHECK(big != NULL, "large alloc 64KiB");
    memset(big, 0x11, 64 * 1024);
    KT_CHECK(kmem_alloc_size(big) >= 64 * 1024, "large usable size");
    kmem_free(big);

    /* Stress churn: allocate and free in a pattern that exercises free lists. */
    for (int round = 0; round < 200; round++) {
        void *p1 = kmem_alloc(32);
        void *p2 = kmem_alloc(512);
        void *p3 = kmem_alloc(1024);
        KT_CHECK(p1 && p2 && p3, "churn alloc");
        kmem_free(p2);
        kmem_free(p1);
        kmem_free(p3);
    }

    KT_RESULT("kmalloc/kfree tests");
}
