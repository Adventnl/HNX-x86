/* Early kernel self-tests. Each test prints [TEST]/[PASS]; a failure prints
 * [PANIC] early test failed: <name> and halts. */
#include "early_tests.h"
#include "kernel.h"
#include "log.h"
#include "panic.h"
#include "cpu.h"
#include "serial.h"
#include "pmm.h"
#include "vmm.h"
#include "paging.h"
#include "heap.h"
#include "memory_layout.h"

#define SCRATCH_VA 0xFFFFFFFFC0000000ULL

static void begin(const char *name) {
    kernel_log("[TEST] ");
    kernel_log_line(name);
}
static void pass(const char *name) {
    kernel_log("[PASS] ");
    kernel_log_line(name);
}
static void fail(const char *name) {
    kernel_log_line("");
    kernel_log("[PANIC] early test failed: ");
    kernel_log_line(name);
    kernel_halt_forever();
}
#define CHECK(cond, name) do { if (!(cond)) { fail(name); } } while (0)

static void test_boot_info(void) {
    const char *n = "boot_info validation";
    begin(n);
    const struct boot_info *bi = kernel_boot_info();
    CHECK(bi != NULL, n);
    CHECK(bi->magic == MYOS_BOOT_INFO_MAGIC, n);
    CHECK(bi->version == MYOS_BOOT_INFO_VERSION, n);
    CHECK(bi->size == sizeof(struct boot_info), n);
    CHECK(bi->framebuffer.width != 0 && bi->framebuffer.height != 0, n);
    pass(n);
}

static void test_string_mem(void) {
    const char *n = "string/memory helpers";
    begin(n);
    char a[32];
    char b[32];
    memset(a, 0xAB, sizeof(a));
    for (unsigned i = 0; i < sizeof(a); i++) {
        CHECK((unsigned char)a[i] == 0xAB, n);
    }
    memcpy(b, a, sizeof(b));
    CHECK(memcmp(a, b, sizeof(a)) == 0, n);
    b[10] = 0;
    CHECK(memcmp(a, b, sizeof(a)) != 0, n);
    CHECK(kstrlen("hello") == 5, n);
    CHECK(kstrlen("") == 0, n);
    pass(n);
}

static void test_serial_log(void) {
    const char *n = "serial/log smoke test";
    begin(n);
    CHECK(serial_is_ready(), n);
    kernel_log_line("    (serial+log router reachable)");
    pass(n);
}

static void test_descriptor_tables(void) {
    const char *n = "GDT/IDT installed checks";
    begin(n);
    struct x86_descriptor_ptr gdtr, idtr;
    cpu_read_gdtr(&gdtr);
    cpu_read_idtr(&idtr);
    CHECK(gdtr.base != 0 && gdtr.limit >= 55, n);      /* 7 entries */
    CHECK(idtr.base != 0 && idtr.limit >= 0xFFF, n);   /* 256 gates */
    pass(n);
}

static void test_pmm(void) {
    const char *n = "PMM allocate/free";
    begin(n);
    uint64_t before = pmm_free_pages();
    uint64_t p1 = pmm_alloc_page();
    CHECK(p1 != PMM_INVALID_PAGE, n);
    CHECK((p1 & PAGE_MASK) == 0, n);
    CHECK(pmm_free_pages() == before - 1, n);
    uint64_t p2 = pmm_alloc_page();
    CHECK(p2 != PMM_INVALID_PAGE && p2 != p1, n);
    pmm_free_page(p1);
    pmm_free_page(p2);
    CHECK(pmm_free_pages() == before, n);
    pass(n);
}

static void test_vmm(void) {
    const char *n = "VMM map/translate";
    begin(n);
    uint64_t phys = pmm_alloc_page();
    CHECK(phys != PMM_INVALID_PAGE, n);

    int rc = vmm_map_page(SCRATCH_VA, phys, PAGE_WRITABLE);
    CHECK(rc == 0, n);

    uint64_t back = vmm_get_physical(SCRATCH_VA);
    CHECK((back & ~PAGE_MASK) == phys, n);

    if (vmm_cr3_loaded()) {
        volatile uint32_t *p = (volatile uint32_t *)SCRATCH_VA;
        *p = 0xDEADBEEFu;
        CHECK(*p == 0xDEADBEEFu, n);
    }

    CHECK(vmm_unmap_page(SCRATCH_VA) == 0, n);
    CHECK(vmm_get_physical(SCRATCH_VA) == PAGING_NO_MAP, n);
    pmm_free_page(phys);
    pass(n);
}

static void test_heap(void) {
    const char *n = "heap allocation";
    begin(n);
    uint8_t *a = (uint8_t *)kmalloc(64);
    CHECK(a != NULL, n);
    for (int i = 0; i < 64; i++) {
        a[i] = (uint8_t)i;
    }
    for (int i = 0; i < 64; i++) {
        CHECK(a[i] == (uint8_t)i, n);
    }
    uint32_t *z = (uint32_t *)kcalloc(16, sizeof(uint32_t));
    CHECK(z != NULL, n);
    for (int i = 0; i < 16; i++) {
        CHECK(z[i] == 0, n);
    }
    pass(n);
}

void early_tests_run(void) {
    test_boot_info();
    test_string_mem();
    test_serial_log();
    test_descriptor_tables();
    test_pmm();
    test_vmm();
    test_heap();
}
