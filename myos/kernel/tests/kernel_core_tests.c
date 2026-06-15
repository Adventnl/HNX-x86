/* Work Unit A aggregator: core init + full test matrix (see header). */
#include "kernel_core_tests.h"
#include "log.h"
#include "slab.h"
#include "object.h"
#include "klog_ring.h"
#include "ktrace.h"
#include "ksym.h"
#include "dump.h"

void kernel_core_init(void) {
    slab_init();
    kobject_subsystem_init();
    klog_ring_init();
    ktrace_init();
    ksym_init();
    debug_dump_init();
    kernel_log_ok("Kernel core allocators online");
    kernel_log_ok("Kernel object model online");
    kernel_log_ok("Kernel debug/trace framework online");
}

void kernel_core_tests_run(void) {
    kernel_log_line("---- Work Unit A: kernel core production tests ----");
    lib_tests_run();
    allocator_tests_run();
    slab_tests_run();
    vm_tests_run();
    sync_tests_run();
    workqueue_tests_run();
    timer_tests_run();
    debug_tests_run();
    kernel_log_ok("Kernel core production foundation online");
}
