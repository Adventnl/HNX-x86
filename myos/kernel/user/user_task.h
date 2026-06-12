/* User task = a kernel thread bound to a user address space (Prompt 4 has no
 * full process model: one thread per task, no fork/exec/wait). */
#ifndef MYOS_USER_TASK_H
#define MYOS_USER_TASK_H

#include "types.h"

struct user_address_space;
struct thread;

enum user_task_state {
    USER_TASK_NEW,
    USER_TASK_RUNNING,
    USER_TASK_EXITED,
    USER_TASK_FAULTED
};

struct user_task {
    uint64_t id;
    const char *name;
    enum user_task_state state;
    int64_t exit_code;

    struct user_address_space *address_space;
    struct thread *thread;

    uint64_t entry_rip;
    uint64_t user_stack_top;
};

/* Build a task from an initramfs HXE1 path (e.g. "/bin/init.hxe"). Returns the
 * task (state NEW) or NULL if the file is missing / malformed. */
struct user_task *user_task_create_from_initramfs(const char *path);

/* Build a task from an in-memory HXE1 image (used by the fault test). */
struct user_task *user_task_create_from_image(const char *name,
                                              const void *image, uint64_t size);

/* Create the backing kernel thread and admit it to the scheduler. */
void user_task_start(struct user_task *task);

/* The user task whose thread is currently running, or NULL in a kernel thread. */
struct user_task *user_current_task(void);

/* Mark the current task exited/faulted, store the code, and schedule away.
 * Never returns to user mode (or at all). */
void user_task_exit_current(int64_t exit_code) __attribute__((noreturn));
void user_task_fault_current(int64_t code) __attribute__((noreturn));

/* Field accessors (kept small for the syscall layer). */
static inline struct user_address_space *user_task_space(struct user_task *t) {
    return t->address_space;
}
static inline uint64_t user_task_id(struct user_task *t) { return t->id; }

#endif /* MYOS_USER_TASK_H */
