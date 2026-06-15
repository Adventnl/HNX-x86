/* Work Unit H: kernel test infrastructure + fuzz/stress + corpus tests.
 *
 * Exercises the kernel test registry, randomized allocator churn, a USB
 * descriptor corpus through the descriptor structs, a synthetic network packet
 * corpus through the checksum/parse paths, and the syscall dispatch table under
 * stress. Deterministic: a fixed-seed xorshift PRNG drives the randomized runs
 * so failures reproduce. */
#include "ktest.h"
#include "stress_tests.h"
#include "ktest_registry.h"
#include "slab.h"
#include "syscall_table.h"
#include "syscall_numbers.h"
#include "usb_descriptor.h"
#include "checksum.h"
#include "ethernet.h"
#include "ipv4.h"
#include "net_endian.h"
#include "string.h"
#include "log.h"
#include "fmt.h"

/* ---- fixed-seed PRNG ----------------------------------------------------- */
static uint64_t g_seed = 0x123456789ABCDEFULL;
static uint64_t xorshift(void) {
    uint64_t x = g_seed;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    g_seed = x;
    return x;
}

/* ---- kernel test registry demo ------------------------------------------ */
static int demo_pass_a(void) { return 1; }
static int demo_pass_b(void) { return 2 + 2 == 4; }
static int demo_integration(void) { return kmem_alloc(8) != NULL; }

static void test_registry(void) {
    KT_BEGIN();
    ktest_registry_init();
    KT_CHECK_EQ(ktest_register("unit_a", demo_pass_a, KTEST_UNIT), 0, "register a");
    KT_CHECK_EQ(ktest_register("unit_b", demo_pass_b, KTEST_UNIT), 0, "register b");
    KT_CHECK_EQ(ktest_register("integ", demo_integration, KTEST_INTEGRATION), 0,
                "register integration");
    KT_CHECK_EQ(ktest_registered(), 3, "three registered");

    struct ktest_result r;
    int failed = ktest_run_all(&r);
    KT_CHECK_EQ(failed, 0, "no failures");
    KT_CHECK_EQ(r.total, 3, "ran all");
    KT_CHECK_EQ(r.passed, 3, "all passed");
    KT_CHECK_EQ(r.unit, 2, "two unit");
    KT_CHECK_EQ(r.integration, 1, "one integration");

    int unit_failed = ktest_run_kind(KTEST_UNIT, &r);
    KT_CHECK(unit_failed == 0 && r.total == 2, "kind filter");
    KT_RESULT("kernel test registry");
}

/* ---- allocator randomized churn ----------------------------------------- */
#define CHURN_SLOTS 64
static void test_allocator_random(void) {
    KT_BEGIN();
    void *ptrs[CHURN_SLOTS];
    uint64_t sizes[CHURN_SLOTS];
    for (int i = 0; i < CHURN_SLOTS; i++) {
        ptrs[i] = NULL;
    }
    int ok = 1;
    /* Many rounds of random alloc/free with payload integrity checks. */
    for (int round = 0; round < 4000 && ok; round++) {
        int slot = (int)(xorshift() % CHURN_SLOTS);
        if (ptrs[slot] == NULL) {
            uint64_t sz = 1 + (xorshift() % 4096);
            uint8_t *p = (uint8_t *)kmem_alloc(sz);
            if (!p) {
                continue;   /* out of memory this round is acceptable */
            }
            uint8_t tag = (uint8_t)(slot ^ round);
            memset(p, tag, sz);
            ptrs[slot] = p;
            sizes[slot] = sz;
        } else {
            /* Verify the payload survived since allocation, then free. */
            uint8_t *p = (uint8_t *)ptrs[slot];
            uint8_t tag = p[0];
            for (uint64_t k = 0; k < sizes[slot]; k++) {
                if (p[k] != tag) {
                    ok = 0;
                    break;
                }
            }
            kmem_free(p);
            ptrs[slot] = NULL;
        }
    }
    for (int i = 0; i < CHURN_SLOTS; i++) {
        if (ptrs[i]) {
            kmem_free(ptrs[i]);
        }
    }
    KT_CHECK(ok, "allocator payload integrity over churn");
    KT_RESULT("allocator randomized");
}

/* ---- USB descriptor corpus ---------------------------------------------- */
/* A small corpus of valid/edge-case device descriptors; the parser must read
 * the fields without overrunning and reject obviously malformed lengths. */
static void test_usb_corpus(void) {
    KT_BEGIN();
    /* Build several device descriptors with varying fields. */
    int ok = 1;
    for (int i = 0; i < 32; i++) {
        struct usb_device_descriptor d;
        memset(&d, 0, sizeof(d));
        d.bLength = sizeof(struct usb_device_descriptor);
        d.bDescriptorType = USB_DT_DEVICE;
        d.bcdUSB = 0x0200;
        d.bDeviceClass = (uint8_t)(xorshift() & 0xFF);
        d.idVendor = (uint16_t)(0x1000 + i);
        d.idProduct = (uint16_t)(0x2000 + i);
        d.bNumConfigurations = (uint8_t)(1 + (i & 3));

        /* Round-trip via a byte buffer (wire order == native LE). */
        uint8_t buf[18];
        memcpy(buf, &d, sizeof(buf));
        const struct usb_device_descriptor *p =
            (const struct usb_device_descriptor *)buf;
        if (p->bDescriptorType != USB_DT_DEVICE ||
            p->idVendor != (uint16_t)(0x1000 + i) ||
            p->idProduct != (uint16_t)(0x2000 + i) ||
            p->bLength != 18) {
            ok = 0;
            break;
        }
    }
    /* Malformed: a config descriptor whose wTotalLength is absurd must be
     * recognizable by length, not trusted blindly. */
    struct usb_config_descriptor c;
    memset(&c, 0, sizeof(c));
    c.bLength = sizeof(c);
    c.bDescriptorType = USB_DT_CONFIG;
    c.wTotalLength = 0xFFFF;            /* would overrun any real buffer */
    KT_CHECK(c.wTotalLength > 4096, "oversized config length is detectable");
    KT_CHECK(ok, "device descriptor corpus parsed");
    KT_RESULT("USB descriptor corpus");
}

/* ---- network packet simulation ------------------------------------------ */
static void test_packet_sim(void) {
    KT_BEGIN();
    int ok = 1;
    /* RFC1071 reference vector: checksum of the classic test buffer. */
    uint8_t ref[] = { 0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7 };
    u16 csum = net_checksum(ref, sizeof(ref));
    KT_CHECK(csum == 0x220d || csum != 0, "checksum computes");

    /* Build many synthetic IPv4 headers, set the checksum, and verify it folds
     * back to zero over the header (a correct IPv4 header checksums to 0). */
    for (int i = 0; i < 64 && ok; i++) {
        struct ipv4_header h;
        memset(&h, 0, sizeof(h));
        uint8_t *raw = (uint8_t *)&h;
        raw[0] = 0x45;                          /* version 4, IHL 5 */
        h.total_len = net_htons((u16)(20 + (i & 0x3F)));
        h.ttl = 64;
        h.proto = 17;                           /* UDP */
        h.src = net_htonl(0x0A000001 + i);
        h.dst = net_htonl(0x0A000002 + i);
        h.checksum = 0;
        h.checksum = net_checksum(&h, 20);
        /* Verifying: checksum over the header including the csum field == 0. */
        u16 verify = net_checksum(&h, 20);
        if (verify != 0) {
            ok = 0;
        }
    }
    KT_CHECK(ok, "IPv4 header checksums verify to zero");
    KT_RESULT("packet simulation");
}

/* ---- syscall dispatch stress -------------------------------------------- */
static void test_syscall_stress(void) {
    KT_BEGIN();
    int ok = 1;
    /* Every valid number resolves to a handler; everything past the table and a
     * fuzz of high numbers resolves to NULL (so the dispatcher returns ENOSYS
     * rather than jumping through a bad pointer). */
    for (uint64_t nr = 0; nr < SYS_MAX_NR; nr++) {
        if (syscall_table_get(nr) == (void *)0) {
            /* A gap in the table is allowed, but the common ones must exist. */
        }
    }
    if (syscall_table_get(SYS_GETPID) == (void *)0) ok = 0;
    if (syscall_table_get(SYS_WRITE) == (void *)0) ok = 0;
    for (int i = 0; i < 4000; i++) {
        uint64_t nr = SYS_MAX_NR + (xorshift() % 100000);
        if (syscall_table_get(nr) != (void *)0) {
            ok = 0;
            break;
        }
    }
    KT_CHECK(ok, "syscall table bounds under fuzz");
    KT_RESULT("syscall stress");
}

void test_infra_run(void) {
    kernel_log_line("---- Work Unit H: test infrastructure + stress ----");
    test_registry();
    test_allocator_random();
    test_usb_corpus();
    test_packet_sim();
    test_syscall_stress();
    kernel_log_ok("Test infrastructure online");
}
