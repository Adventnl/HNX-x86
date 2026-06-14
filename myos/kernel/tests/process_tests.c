/* Kernel-side process-table checks: alloc assigns a PID + slot, free clears it,
 * and a kernel thread has no current process. */
#include "process_tests.h"
#include "process.h"
#include "process_table.h"
#include "log.h"

void process_tests_run(void) {
    int ok = 1;

    struct process *p = process_table_alloc();
    if (!p || p->pid == 0) {
        ok = 0;
    } else {
        uint64_t pid = p->pid;
        if (process_table_get(pid) != p) {
            ok = 0;
        }
        process_table_free(p);
        if (process_table_get(pid) != NULL) {
            ok = 0;
        }
    }

    if (process_current() != NULL) {
        ok = 0;   /* boot/kernel context owns no process */
    }

    if (ok) {
        kernel_log_line("[PASS] kernel process table");
    } else {
        kernel_log_error("kernel process table test failed");
    }
}
