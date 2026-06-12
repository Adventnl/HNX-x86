/* User task lifecycle: load an HXE1 image into a fresh address space, attach a
 * kernel thread, and transition that thread into ring 3. */
#include "user_task.h"
#include "user.h"
#include "user_address_space.h"
#include "user_loader.h"
#include "initramfs.h"
#include "thread.h"
#include "scheduler.h"
#include "heap.h"
#include "paging.h"
#include "log.h"
#include "panic.h"

static uint64_t g_next_id = 1;

/* Kernel-thread trampoline: runs in ring 0 (under the task's CR3, which mirrors
 * the kernel) just long enough to flip into ring 3 at the user entry point. */
static void user_thread_entry(void *arg) {
    struct user_task *task = (struct user_task *)arg;
    task->state = USER_TASK_RUNNING;
    user_enter_ring3(task->entry_rip, task->user_stack_top,
                     user_address_space_cr3(task->address_space));
}

struct user_task *user_task_create_from_image(const char *name,
                                              const void *image, uint64_t size) {
    struct user_address_space *space = user_address_space_create();
    if (!space) {
        return NULL;
    }
    uint64_t entry = 0;
    if (user_loader_load(space, image, size, &entry) != 0) {
        kernel_log_error("user: HXE1 load failed");
        user_address_space_destroy(space);
        return NULL;
    }
    if (user_map_range(space, USER_STACK_TOP - USER_STACK_SIZE,
                       USER_STACK_SIZE, PAGE_WRITABLE) != 0) {
        kernel_log_error("user: stack mapping failed");
        user_address_space_destroy(space);
        return NULL;
    }

    struct user_task *task = (struct user_task *)kcalloc(1, sizeof(*task));
    if (!task) {
        user_address_space_destroy(space);
        return NULL;
    }
    task->id = g_next_id++;
    task->name = name;
    task->state = USER_TASK_NEW;
    task->exit_code = 0;
    task->address_space = space;
    task->entry_rip = entry;
    task->user_stack_top = USER_STACK_TOP;
    return task;
}

struct user_task *user_task_create_from_initramfs(const char *path) {
    uint64_t size = 0;
    const void *image = initramfs_find(path, &size);
    if (!image) {
        kernel_log("[ERROR] user: initramfs file not found: ");
        kernel_log_line(path);
        return NULL;
    }
    return user_task_create_from_image(path, image, size);
}

void user_task_start(struct user_task *task) {
    if (!task) {
        return;
    }
    struct thread *t = thread_create_raw(task->name, user_thread_entry, task);
    if (!t) {
        kernel_panic("user: cannot create task thread");
    }
    t->cr3 = user_address_space_cr3(task->address_space);
    t->user_task = task;
    task->thread = t;
    scheduler_make_ready(t);
}

struct user_task *user_current_task(void) {
    struct thread *t = thread_current();
    return t ? (struct user_task *)t->user_task : NULL;
}

void user_task_exit_current(int64_t exit_code) {
    struct user_task *task = user_current_task();
    if (task) {
        task->state = USER_TASK_EXITED;
        task->exit_code = exit_code;
    }
    thread_exit();   /* marks the thread DEAD and reschedules; never returns */
}

void user_task_fault_current(int64_t code) {
    struct user_task *task = user_current_task();
    if (task) {
        task->state = USER_TASK_FAULTED;
        task->exit_code = code;
    }
    thread_exit();
}
