/* Ring-3 fault isolation. Invoked from x86_exception_dispatch when a fault was
 * taken in user mode (CS.RPL == 3): print a diagnostic, terminate the offending
 * process, and reschedule. The kernel keeps running. */
#include "user_fault.h"
#include "process.h"
#include "exceptions.h"
#include "cpu.h"
#include "log.h"
#include "panic.h"

void user_fault_handle(struct x86_trap_frame *frame) {
    x86_cli();

    struct process *p = process_current();

    kernel_log_line("");
    kernel_log_line("-------------------- USER FAULT --------------------");
    kernel_log_hex64("  vector     : ", frame->vector);
    kernel_log("  name       : ");
    kernel_log_line(x86_exception_name(frame->vector));
    kernel_log_hex64("  error code : ", frame->error_code);
    kernel_log_hex64("  rip        : ", frame->rip);
    kernel_log_hex64("  rsp        : ", frame->rsp);
    kernel_log_hex64("  cs         : ", frame->cs);
    if (frame->vector == VEC_PAGE_FAULT) {
        kernel_log_hex64("  fault addr : ", x86_read_cr2());
    }
    if (p) {
        kernel_log("  process    : ");
        kernel_log_line(p->name[0] ? p->name : "(unnamed)");
        kernel_log_hex64("  pid        : ", p->pid);
    }

    /* The kernel survives: terminate the process and reschedule. */
    kernel_log_ok("User fault isolated");

    if (!p) {
        /* A CPL-3 fault with no current process should be impossible. */
        kernel_panic("user fault with no current process");
    }
    process_fault_current(-(int64_t)frame->vector);   /* never returns */
}
