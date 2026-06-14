/* Userland supervisor: a kernel thread that spawns the init process (PID 1),
 * waits for it to exit, and announces the final pass marker. Init drives the
 * rest of the userland test matrix from ring 3 (syscall/fd/vfs/spawn/fault
 * tests + the scripted shell). */
#include "user_tests.h"
#include "process.h"
#include "thread.h"
#include "sleep.h"
#include "log.h"

static void supervisor_entry(void *arg) {
    (void)arg;

    int pid = process_spawn("/bin/init.hxe");
    if (pid < 0) {
        kernel_log_error("could not spawn /bin/init.hxe");
        for (;;) {
            thread_sleep_ms(1000);
        }
    }

    int64_t code = 0;
    process_wait((uint64_t)pid, &code);

    if (code == 0) {
        kernel_log_ok("Userland foundation tests passed");
    } else {
        kernel_log_hex64("[ERROR] init exit code: ", (uint64_t)code);
    }

    for (;;) {
        thread_sleep_ms(1000);
    }
}

void user_tests_start(void) {
    if (!thread_create("user-supervisor", supervisor_entry, NULL)) {
        kernel_log_error("could not create user supervisor thread");
    }
}
