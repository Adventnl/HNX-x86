/* Ring-3 fault isolation. Invoked from x86_exception_dispatch when a fault was
 * taken in user mode (CS.RPL == 3). */
#include "user_fault.h"
#include "user_task.h"
#include "exceptions.h"
#include "cpu.h"
#include "log.h"
#include "panic.h"

void user_fault_handle(struct x86_trap_frame *frame) {
    x86_cli();

    struct user_task *task = user_current_task();

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
    if (task) {
        kernel_log("  task       : ");
        kernel_log_line(task->name ? task->name : "(unnamed)");
    }

    /* The kernel survives: terminate the task and reschedule. */
    kernel_log_ok("User fault isolated");

    if (!task) {
        /* A CPL-3 fault with no current user task should be impossible. */
        kernel_panic("user fault with no current task");
    }
    user_task_fault_current(-(int64_t)frame->vector);   /* never returns */
}
