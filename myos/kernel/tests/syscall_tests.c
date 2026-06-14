/* Kernel-side syscall-dispatch checks: the bad-number path and table bounds. */
#include "syscall_tests.h"
#include "syscall.h"
#include "syscall_table.h"
#include "syscall_numbers.h"
#include "string.h"
#include "log.h"

void syscall_tests_run(void) {
    int ok = 1;

    struct syscall_frame f;
    memset(&f, 0, sizeof(f));
    f.rax = 0xDEAD;
    if (syscall_dispatch(&f) != -SYS_ENOSYS) {
        ok = 0;
    }
    if (syscall_table_get(SYS_WRITE) == (syscall_fn)0) {
        ok = 0;
    }
    if (syscall_table_get(9999) != (syscall_fn)0) {
        ok = 0;
    }

    if (ok) {
        kernel_log_line("[PASS] kernel syscall dispatch");
    } else {
        kernel_log_error("kernel syscall dispatch test failed");
    }
}
