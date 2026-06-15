/* Tests for the VM region tracker: insert/find/overlap/free-gap/remove/split,
 * protection changes, COW marking and fault accounting. Marker: "VM region tests". */
#include "ktest.h"
#include "vmregion.h"
#include "memory_layout.h"

#define MB (1024ULL * 1024ULL)

void vm_tests_run(void) {
    KT_BEGIN();

    struct vm_map m;
    vm_map_init(&m, 0x10000000ULL, 0x80000000ULL, "test");
    KT_CHECK_EQ(m.region_count, 0, "empty map");

    /* Insert a few regions. */
    KT_CHECK_EQ(vm_map_insert(&m, 0x20000000, 0x20100000,
                              VM_PROT_READ | VM_PROT_WRITE, VM_FLAG_ANON, "data"),
                0, "insert data");
    KT_CHECK_EQ(vm_map_insert(&m, 0x30000000, 0x30040000,
                              VM_PROT_READ | VM_PROT_EXEC, VM_FLAG_FILE, "text"),
                0, "insert text");
    KT_CHECK_EQ(m.region_count, 2, "two regions");

    /* Overlap is rejected. */
    KT_CHECK_EQ(vm_map_insert(&m, 0x20080000, 0x20180000, VM_PROT_READ, 0, "x"),
                -1, "overlap rejected");
    /* Out-of-range is rejected. */
    KT_CHECK_EQ(vm_map_insert(&m, 0x90000000, 0x90001000, VM_PROT_READ, 0, "x"),
                -1, "out of range rejected");
    /* Misaligned is rejected. */
    KT_CHECK_EQ(vm_map_insert(&m, 0x40000001, 0x40002000, VM_PROT_READ, 0, "x"),
                -1, "misaligned rejected");

    /* Find. */
    struct vm_region *r = vm_map_find(&m, 0x20050000);
    KT_CHECK(r != NULL, "find inside data");
    KT_CHECK(r->flags & VM_FLAG_ANON, "found correct region");
    KT_CHECK(vm_map_find(&m, 0x25000000) == NULL, "find in gap is NULL");

    /* Find a free gap. */
    uint64_t gap = vm_map_find_free(&m, 0x10000000, MB);
    KT_CHECK(gap != 0, "found free gap");
    KT_CHECK(!vm_map_overlaps(&m, gap, gap + MB), "gap is free");

    /* Protection change. */
    KT_CHECK_EQ(vm_map_protect(&m, 0x30000000, 0x30040000, VM_PROT_READ), 0,
                "protect text RO");
    r = vm_map_find(&m, 0x30000000);
    KT_CHECK(r && r->prot == VM_PROT_READ, "text now RO");

    /* COW marking. */
    int cow = vm_map_mark_cow(&m, 0x20000000, 0x20100000);
    KT_CHECK_EQ(cow, 1, "cow marked one region");
    r = vm_map_find(&m, 0x20000000);
    KT_CHECK(r && (r->flags & VM_FLAG_COW), "data is COW");

    /* Split via punch-hole remove. */
    uint64_t removed = vm_map_remove(&m, 0x20040000, 0x20080000);
    KT_CHECK_EQ(removed, 0x40000, "removed 256KiB hole");
    KT_CHECK_EQ(m.region_count, 3, "split created a region");
    KT_CHECK(vm_map_find(&m, 0x20060000) == NULL, "hole is unmapped");
    KT_CHECK(vm_map_find(&m, 0x20020000) != NULL, "left part remains");
    KT_CHECK(vm_map_find(&m, 0x200A0000) != NULL, "right part remains");

    /* Fault accounting. */
    vm_map_account_fault(&m, 0x20020000, 0, 1);     /* minor read fault */
    vm_map_account_fault(&m, 0x20020000, 1, 1);     /* write to COW */
    vm_map_account_fault(&m, 0x55000000, 1, 0);     /* fault in a gap */
    KT_CHECK(m.cow_faults >= 1, "cow fault counted");
    KT_CHECK(m.major_faults >= 1, "gap fault counted as major");

    vm_map_destroy(&m);
    KT_CHECK_EQ(m.region_count, 0, "destroy clears regions");

    KT_RESULT("VM region tests");
}
