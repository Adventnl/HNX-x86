/* System-call dispatcher + the Prompt 4 syscall table.
 *
 * Handlers validate every user pointer in software (the running CR3 is the
 * calling task's, so a bad pointer must never be dereferenced by the kernel —
 * that would fault in ring 0 and be misreported as a kernel fault). Bad user
 * input returns a negative errno; it never panics. */
#include "syscall.h"
#include "user.h"
#include "user_task.h"
#include "user_address_space.h"
#include "idt.h"
#include "gdt.h"
#include "scheduler.h"
#include "sleep.h"
#include "framebuffer_console.h"
#include "serial.h"
#include "log.h"

void syscall_init(void) {
    idt_set_gate(SYSCALL_VECTOR, (void *)syscall_entry,
                 GDT_KERNEL_CODE, IDT_FLAG_INTERRUPT_GATE_DPL3);
    kernel_log_ok("Syscall vector installed");
    kernel_log_ok("Syscall dispatcher online");
}

/* Emit one byte to the kernel console sinks (framebuffer + serial). */
static void console_putc(char c) {
    fbcon_putc(c);
    serial_write_char(c);
}

static int64_t sys_write(uint64_t fd, uint64_t user_buffer, uint64_t length) {
    if (fd != 1 && fd != 2) {           /* only stdout/stderr in Prompt 4 */
        return -SYS_EBADF;
    }
    if (length == 0) {
        return 0;
    }
    struct user_task *task = user_current_task();
    if (!task) {
        return -SYS_EFAULT;
    }
    struct user_address_space *space = user_task_space(task);
    if (!user_range_is_valid(space, user_buffer, length, 0)) {
        return -SYS_EFAULT;
    }
    /* Copy from user in chunks, emitting to the logger sinks. */
    const char *p = (const char *)(uintptr_t)user_buffer;
    char chunk[64];
    uint64_t done = 0;
    while (done < length) {
        uint64_t n = length - done;
        if (n > sizeof(chunk)) {
            n = sizeof(chunk);
        }
        for (uint64_t i = 0; i < n; i++) {
            chunk[i] = p[done + i];
        }
        for (uint64_t i = 0; i < n; i++) {
            console_putc(chunk[i]);
        }
        done += n;
    }
    return (int64_t)length;
}

static int64_t sys_read(uint64_t fd, uint64_t user_buffer, uint64_t length) {
    if (length == 0) {
        return 0;
    }
    struct user_task *task = user_current_task();
    if (!task) {
        return -SYS_EFAULT;
    }
    if (!user_range_is_valid(user_task_space(task), user_buffer, length, 1)) {
        return -SYS_EFAULT;
    }
    if (fd == 0) {
        return 0;                       /* no console input yet: EOF */
    }
    return -SYS_EBADF;
}

static int64_t sys_exit(uint64_t code) {
    user_task_exit_current((int64_t)code);   /* never returns */
    return 0;
}

static int64_t sys_yield(void) {
    scheduler_yield();
    return 0;
}

static int64_t sys_sleep(uint64_t milliseconds) {
    thread_sleep_ms(milliseconds);
    return 0;
}

static int64_t sys_getpid(void) {
    struct user_task *task = user_current_task();
    return task ? (int64_t)user_task_id(task) : -SYS_EFAULT;
}

int64_t syscall_dispatch(struct syscall_frame *frame) {
    switch (frame->rax) {
    case SYS_EXIT:   return sys_exit(frame->rdi);
    case SYS_WRITE:  return sys_write(frame->rdi, frame->rsi, frame->rdx);
    case SYS_READ:   return sys_read(frame->rdi, frame->rsi, frame->rdx);
    case SYS_SLEEP:  return sys_sleep(frame->rdi);
    case SYS_GETPID: return sys_getpid();
    case SYS_YIELD:  return sys_yield();
    default:         return -SYS_ENOSYS;
    }
}
