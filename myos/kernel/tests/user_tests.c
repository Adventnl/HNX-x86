/* User/kernel boundary tests.
 *
 * A dedicated supervisor kernel thread launches each initramfs user program,
 * sleeps until it leaves the running state, and checks its exit code. It also
 * exercises the dispatcher's invalid-syscall path directly. With
 * MYOS_TEST_USER_FAULT defined it first runs a synthetic program that
 * dereferences an unmapped user address to prove fault isolation. */
#include "user_tests.h"
#include "user_task.h"
#include "user_loader.h"
#include "user.h"
#include "syscall.h"
#include "thread.h"
#include "scheduler.h"
#include "sleep.h"
#include "kernel.h"
#include "log.h"

static void wait_for_task(struct user_task *t) {
    while (t->state == USER_TASK_NEW || t->state == USER_TASK_RUNNING) {
        thread_sleep_ms(5);
    }
}

/* Run an initramfs program to completion; return its exit code (or a negative
 * sentinel if it could not be created/started). */
static int64_t run_program(const char *path) {
    struct user_task *t = user_task_create_from_initramfs(path);
    if (!t) {
        return -1000;
    }
    user_task_start(t);
    wait_for_task(t);
    return (t->state == USER_TASK_EXITED) ? t->exit_code : -1001;
}

#ifdef MYOS_TEST_USER_FAULT
/* Synthetic HXE1 image: a single RX page at USER_IMAGE_BASE whose code writes
 * to the unmapped user address 0, forcing a ring-3 #PF. */
static void run_fault_program(void) {
    static const uint8_t code[] = {
        0x48, 0x31, 0xC0,                         /* xor  %rax, %rax        */
        0x48, 0xC7, 0x00, 0x00, 0x00, 0x00, 0x00, /* movq $0, (%rax)  -> #PF */
        0xEB, 0xFE                                /* 1: jmp 1b (unreached)  */
    };
    uint8_t img[128];
    struct hxe_header *h = (struct hxe_header *)img;
    struct hxe_segment *s = (struct hxe_segment *)(img + sizeof(*h));
    h->magic = HXE_MAGIC;
    h->version = HXE_VERSION;
    h->entry = USER_IMAGE_BASE;
    h->segment_count = 1;
    h->header_size = sizeof(*h);
    s->virtual_address = USER_IMAGE_BASE;
    s->memory_size = 0x1000;
    s->file_size = sizeof(code);
    s->file_offset = sizeof(*h) + sizeof(*s);
    s->flags = HXE_SEG_READ | HXE_SEG_EXEC;
    memcpy(img + s->file_offset, code, sizeof(code));

    kernel_log_line("[TEST] user fault isolation");
    struct user_task *t = user_task_create_from_image("/fault", img,
                                                       s->file_offset + sizeof(code));
    if (!t) {
        kernel_log_error("user fault test: could not create task");
        return;
    }
    user_task_start(t);
    wait_for_task(t);
    if (t->state == USER_TASK_FAULTED) {
        kernel_log_line("[PASS] user fault isolation (task terminated, kernel alive)");
    } else {
        kernel_log_error("user fault test: task did not fault");
    }
}
#endif

/* Kernel-side check: an unknown syscall number returns -ENOSYS, never panics. */
static void test_invalid_syscall(void) {
    struct syscall_frame f;
    memset(&f, 0, sizeof(f));
    f.rax = 0xDEAD;
    int64_t r = syscall_dispatch(&f);
    if (r != -SYS_ENOSYS) {
        kernel_log_error("invalid syscall did not return -ENOSYS");
    } else {
        kernel_log_line("[PASS] invalid syscall -> -ENOSYS");
    }
}

static void supervisor_entry(void *arg) {
    (void)arg;

#ifdef MYOS_TEST_USER_FAULT
    run_fault_program();
#endif

    int64_t init_code = run_program("/bin/init.hxe");
    if (init_code == 0) {
        kernel_log_ok("First user program exited cleanly");
    } else {
        kernel_log_hex64("[ERROR] init exit code: ", (uint64_t)init_code);
    }

    int64_t test_code = run_program("/bin/syscall_test.hxe");
    if (test_code == 0) {
        kernel_log_line("[PASS] syscall_test exited 0");
    } else {
        kernel_log_hex64("[ERROR] syscall_test exit code: ", (uint64_t)test_code);
    }

    test_invalid_syscall();

    if (init_code == 0 && test_code == 0) {
        kernel_log_ok("User/kernel boundary tests passed");
    } else {
        kernel_log_error("User/kernel boundary tests FAILED");
    }

    /* Done; idle forever so the scheduler keeps running. */
    for (;;) {
        thread_sleep_ms(1000);
    }
}

void user_tests_start(void) {
    if (!thread_create("user-supervisor", supervisor_entry, NULL)) {
        kernel_log_error("could not create user supervisor thread");
    }
}
