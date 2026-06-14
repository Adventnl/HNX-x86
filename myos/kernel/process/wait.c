/* process_wait: block until a child terminates, then reap it.
 *
 * v0 uses cooperative polling: the parent sleeps in short increments while the
 * child runs and eventually transitions to EXITED/FAULTED. Reaping frees the
 * child's address space (PMM page tables/frames) and fd table; the page-table
 * teardown runs with the kernel CR3 active. */
#include "wait.h"
#include "process_table.h"
#include "fd_table.h"
#include "user_address_space.h"
#include "user_copy.h"
#include "sleep.h"
#include "syscall_numbers.h"

int process_wait(uint64_t pid, int64_t *exit_code) {
    struct process *self = process_current();
    struct process *child = process_by_pid(pid);
    if (!child) {
        return -SYS_ECHILD;
    }
    if (self && child->parent_pid != self->pid) {
        return -SYS_ECHILD;
    }

    while (child->state != PROCESS_EXITED && child->state != PROCESS_FAULTED) {
        thread_sleep_ms(2);
    }

    if (exit_code) {
        *exit_code = child->exit_code;
    }

    /* Reap: release the child's resources and free its slot. */
    uint64_t saved = user_with_kernel_cr3();
    if (child->address_space) {
        user_address_space_destroy(child->address_space);
        child->address_space = NULL;
    }
    if (child->fds) {
        fd_table_destroy(child->fds);
        child->fds = NULL;
    }
    process_table_free(child);
    user_restore_cr3(saved);

    return (int)pid;
}
