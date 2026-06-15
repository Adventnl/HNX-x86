/* Unit tests for kernel/lib: list, hashtable, bitmap, ring buffer, plus the
 * radix tree, id allocator and string builder. Emits the markers grepped by
 * verify-kernel-core-expanded. */
#include "ktest.h"
#include "list.h"
#include "queue.h"
#include "hashtable.h"
#include "bitmap.h"
#include "ringbuf.h"
#include "radix.h"
#include "idr.h"
#include "strbuf.h"
#include "string.h"

struct item {
    int value;
    struct list_node link;
};

static int cmp_item(const struct list_node *a, const struct list_node *b) {
    const struct item *ia = list_entry(a, struct item, link);
    const struct item *ib = list_entry(b, struct item, link);
    return ia->value - ib->value;
}

static void test_list(void) {
    KT_BEGIN();
    LIST_HEAD(head);
    KT_CHECK(list_empty(&head), "new list empty");

    struct item items[5];
    for (int i = 0; i < 5; i++) {
        items[i].value = i;
        list_add_tail(&items[i].link, &head);
    }
    KT_CHECK_EQ(list_length(&head), 5, "length after 5 adds");
    KT_CHECK(list_validate(&head), "list intact");

    /* Iterate and sum. */
    int sum = 0;
    struct item *it;
    list_for_each_entry(it, &head, link) {
        sum += it->value;
    }
    KT_CHECK_EQ(sum, 10, "sum 0..4");

    /* Remove the middle node. */
    list_del(&items[2].link);
    KT_CHECK_EQ(list_length(&head), 4, "length after delete");
    KT_CHECK(list_validate(&head), "list intact after delete");

    /* First/last. */
    KT_CHECK_EQ(list_first_entry(&head, struct item, link)->value, 0, "first");
    KT_CHECK_EQ(list_last_entry(&head, struct item, link)->value, 4, "last");

    /* Re-add and sort descending input -> ascending. */
    LIST_HEAD(h2);
    struct item s[4];
    int order[4] = { 3, 1, 4, 2 };
    for (int i = 0; i < 4; i++) {
        s[i].value = order[i];
        list_add_tail(&s[i].link, &h2);
    }
    list_sort(&h2, cmp_item);
    KT_CHECK(list_validate(&h2), "sorted list intact");
    int prev = -1, ok = 1;
    list_for_each_entry(it, &h2, link) {
        if (it->value < prev) {
            ok = 0;
        }
        prev = it->value;
    }
    KT_CHECK(ok, "list sorted ascending");

    /* Queue wrapper. */
    struct queue q;
    queue_init(&q);
    struct item qi[3];
    for (int i = 0; i < 3; i++) {
        qi[i].value = i + 10;
        queue_enqueue(&q, &qi[i].link);
    }
    KT_CHECK_EQ(queue_length(&q), 3, "queue length");
    struct list_node *n = queue_dequeue(&q);
    KT_CHECK_EQ(queue_entry(n, struct item, link)->value, 10, "FIFO order");
    KT_CHECK_EQ(queue_length(&q), 2, "queue length after dequeue");

    KT_RESULT("lib list tests");
}

static void count_cb(uint64_t key, void *value, void *ctx) {
    (void)key; (void)value;
    (*(int *)ctx)++;
}

static void test_hashtable(void) {
    KT_BEGIN();
    struct hashtable ht;
    KT_CHECK_EQ(hashtable_init(&ht, 16), 0, "ht init");

    for (uint64_t i = 1; i <= 100; i++) {
        KT_CHECK_EQ(hashtable_put(&ht, i, (void *)(uintptr_t)(i * 7)), 0, "put");
    }
    KT_CHECK_EQ(hashtable_count(&ht), 100, "count after 100 puts");

    int found = 0;
    void *v = hashtable_get(&ht, 42, &found);
    KT_CHECK(found, "found key 42");
    KT_CHECK_EQ((uintptr_t)v, 42 * 7, "value of key 42");

    /* Overwrite. */
    hashtable_put(&ht, 42, (void *)(uintptr_t)999);
    KT_CHECK_EQ((uintptr_t)hashtable_get(&ht, 42, NULL), 999, "overwrite value");
    KT_CHECK_EQ(hashtable_count(&ht), 100, "count unchanged on overwrite");

    /* Miss. */
    KT_CHECK(!hashtable_contains(&ht, 9999), "miss for absent key");

    /* Remove. */
    void *r = hashtable_remove(&ht, 50);
    KT_CHECK_EQ((uintptr_t)r, 50 * 7, "removed value");
    KT_CHECK_EQ(hashtable_count(&ht), 99, "count after remove");
    KT_CHECK(!hashtable_contains(&ht, 50), "removed key absent");

    /* foreach visits all. */
    int seen = 0;
    hashtable_foreach(&ht, count_cb, &seen);
    KT_CHECK_EQ(seen, 99, "foreach visits all");

    hashtable_destroy(&ht);
    KT_RESULT("hash table tests");
}

static void test_bitmap(void) {
    KT_BEGIN();
    uint64_t words[BITMAP_WORDS(256)];
    struct bitmap b;
    bitmap_attach(&b, words, 256);
    bitmap_zero(&b);
    KT_CHECK_EQ(bitmap_weight(&b), 0, "zeroed weight");
    KT_CHECK_EQ(bitmap_find_first_zero(&b), 0, "first zero at 0");

    bitmap_set(&b, 5);
    bitmap_set(&b, 200);
    KT_CHECK(bitmap_test(&b, 5), "bit 5 set");
    KT_CHECK(bitmap_test(&b, 200), "bit 200 set");
    KT_CHECK(!bitmap_test(&b, 6), "bit 6 clear");
    KT_CHECK_EQ(bitmap_weight(&b), 2, "weight 2");
    KT_CHECK_EQ(bitmap_find_first_set(&b), 5, "first set at 5");

    bitmap_clear(&b, 5);
    KT_CHECK(!bitmap_test(&b, 5), "bit 5 cleared");

    /* Range ops. */
    bitmap_zero(&b);
    bitmap_set_range(&b, 10, 20);
    KT_CHECK_EQ(bitmap_weight(&b), 20, "range set weight");
    KT_CHECK(bitmap_test(&b, 10) && bitmap_test(&b, 29), "range bounds");
    KT_CHECK(!bitmap_test(&b, 9) && !bitmap_test(&b, 30), "range exclusive");

    /* Zero-run finder. */
    bitmap_fill(&b);
    bitmap_clear_range(&b, 100, 8);
    KT_CHECK_EQ(bitmap_find_zero_run(&b, 8), 100, "8-bit zero run at 100");
    KT_CHECK_EQ(bitmap_find_zero_run(&b, 9), BITMAP_NONE, "no 9-bit run");

    /* Alloc. */
    bitmap_zero(&b);
    size_t a0 = bitmap_alloc(&b);
    size_t a1 = bitmap_alloc(&b);
    KT_CHECK_EQ(a0, 0, "alloc 0");
    KT_CHECK_EQ(a1, 1, "alloc 1");
    KT_CHECK(bitmap_test(&b, 0) && bitmap_test(&b, 1), "allocated bits set");

    KT_RESULT("bitmap tests");
}

static void test_ringbuf(void) {
    KT_BEGIN();
    uint8_t storage[8];
    struct ringbuf r;
    ringbuf_init(&r, storage, 8);
    KT_CHECK(ringbuf_empty(&r), "new ring empty");

    for (int i = 0; i < 8; i++) {
        KT_CHECK(ringbuf_putc(&r, (uint8_t)i), "putc within capacity");
    }
    KT_CHECK(ringbuf_full(&r), "full after 8");
    KT_CHECK(!ringbuf_putc(&r, 99), "putc rejected when full");

    uint8_t c;
    KT_CHECK(ringbuf_getc(&r, &c) && c == 0, "getc returns first");
    KT_CHECK(ringbuf_putc(&r, 8), "putc after getc");

    /* Drain and verify FIFO ordering 1..8. */
    int ok = 1;
    for (int i = 1; i <= 8; i++) {
        if (!ringbuf_getc(&r, &c) || c != (uint8_t)i) {
            ok = 0;
        }
    }
    KT_CHECK(ok, "FIFO 1..8 order");
    KT_CHECK(ringbuf_empty(&r), "empty after drain");

    /* Bulk + force/overwrite. */
    uint8_t src[5] = { 10, 11, 12, 13, 14 };
    KT_CHECK_EQ(ringbuf_write(&r, src, 5), 5, "bulk write 5");
    uint8_t dst[5];
    KT_CHECK_EQ(ringbuf_read(&r, dst, 5), 5, "bulk read 5");
    KT_CHECK(memcmp(src, dst, 5) == 0, "bulk roundtrip");

    ringbuf_reset(&r);
    for (int i = 0; i < 10; i++) {
        ringbuf_force_putc(&r, (uint8_t)i);  /* overwrites oldest */
    }
    KT_CHECK(ringbuf_full(&r), "force keeps it full");
    ringbuf_getc(&r, &c);
    KT_CHECK_EQ(c, 2, "oldest two dropped by force");

    KT_RESULT("ring buffer tests");
}

static void test_radix_and_idr(void) {
    /* Folded into the lib suite output via dedicated checks; their pass/fail is
     * surfaced through the list marker's siblings below. */
    KT_BEGIN();
    struct radix_tree t;
    radix_init(&t);
    for (uint64_t k = 0; k < 500; k++) {
        KT_CHECK_EQ(radix_insert(&t, k * 3, (void *)(uintptr_t)(k + 1)), 0, "radix insert");
    }
    KT_CHECK_EQ(radix_count(&t), 500, "radix count");
    KT_CHECK_EQ((uintptr_t)radix_lookup(&t, 30), 11, "radix lookup");
    KT_CHECK(radix_lookup(&t, 31) == NULL, "radix miss");
    KT_CHECK_EQ((uintptr_t)radix_remove(&t, 30), 11, "radix remove");
    KT_CHECK(radix_lookup(&t, 30) == NULL, "radix removed gone");
    KT_CHECK_EQ(radix_count(&t), 499, "radix count after remove");
    radix_destroy(&t);

    struct idr idr;
    KT_CHECK_EQ(idr_init(&idr, 100, 64), 0, "idr init");
    int a = idr_alloc(&idr);
    int b = idr_alloc(&idr);
    KT_CHECK_EQ(a, 100, "idr first id");
    KT_CHECK_EQ(b, 101, "idr second id");
    KT_CHECK(idr_in_use(&idr, 100), "idr in use");
    idr_free(&idr, 100);
    KT_CHECK(!idr_in_use(&idr, 100), "idr freed");
    KT_CHECK_EQ(idr_reserve(&idr, 150), 0, "idr reserve");
    KT_CHECK_EQ(idr_reserve(&idr, 150), -1, "idr double reserve fails");
    idr_destroy(&idr);

    struct strbuf sb;
    char buf[64];
    strbuf_init(&sb, buf, sizeof(buf));
    strbuf_puts(&sb, "x=");
    strbuf_put_u64(&sb, 42);
    strbuf_puts(&sb, " h=");
    strbuf_put_hex(&sb, 0xBEEF);
    KT_CHECK(strcmp(buf, "x=42 h=0xbeef") == 0, "strbuf content");

    KT_RESULT("lib radix/idr/strbuf tests");
}

void lib_tests_run(void) {
    test_list();
    test_hashtable();
    test_bitmap();
    test_ringbuf();
    test_radix_and_idr();
}
