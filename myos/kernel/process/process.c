/* Process lifecycle: create (load image, build address space/stack/heap/fds),
 * spawn (admit to scheduler), exit and fault.
 *
 * Creation and reaping touch arbitrary physical RAM (page tables, child frames),
 * so they run with the kernel CR3 active — see process_spawn_argv / wait.c. The
 * caller never sleeps inside such a window. */
#include "process.h"
#include "process_table.h"
#include "fd_table.h"
#include "exec.h"
#include "user.h"
#include "user_address_space.h"
#include "user_copy.h"
#include "vfs.h"
#include "file.h"
#include "path.h"
#include "inode.h"
#include "thread.h"
#include "scheduler.h"
#include "paging.h"
#include "heap.h"
#include "string.h"
#include "log.h"
#include "panic.h"
#include "syscall_numbers.h"
#include "syscall_abi.h"

void process_system_init(void) {
    process_table_init();
}

/* Build the argv block on the new stack; returns the initial user RSP (points at
 * argc) or 0 on failure. SysV-ish layout: [argc][argv0..][NULL][strings]. */
static uint64_t build_user_stack(struct user_address_space *space,
                                 int argc, char *const argv[]) {
    if (argc < 0) {
        argc = 0;
    }
    if (argc > PROCESS_MAX_ARGS) {
        argc = PROCESS_MAX_ARGS;
    }
    uint64_t sp = USER_STACK_TOP;
    uint64_t addrs[PROCESS_MAX_ARGS];

    for (int i = 0; i < argc; i++) {
        uint64_t len = strlen(argv[i]) + 1;
        sp -= len;
        if (user_copy_to_space(space, sp, argv[i], len) != 0) {
            return 0;
        }
        addrs[i] = sp;
    }
    sp &= ~0xFULL;

    uint64_t slots = (uint64_t)argc + 2;   /* argc + argv ptrs + NULL */
    if (slots & 1ULL) {
        sp -= 8;                           /* pad so the final RSP is 16-aligned */
    }
    sp -= slots * 8;
    uint64_t base = sp;

    uint64_t argc_word = (uint64_t)argc;
    if (user_copy_to_space(space, base, &argc_word, 8) != 0) {
        return 0;
    }
    for (int i = 0; i < argc; i++) {
        if (user_copy_to_space(space, base + 8 + (uint64_t)i * 8, &addrs[i], 8) != 0) {
            return 0;
        }
    }
    uint64_t nul = 0;
    if (user_copy_to_space(space, base + 8 + (uint64_t)argc * 8, &nul, 8) != 0) {
        return 0;
    }
    return base;
}

/* Wire fds 0/1/2 to /dev/console (shared, refcounted). */
static void wire_stdio(struct process *p) {
    struct vnode *con = vfs_resolve("/dev/console");
    if (!con) {
        return;
    }
    struct file *f = file_alloc(con, O_RDWR, "/dev/console");
    if (!f) {
        return;
    }
    fd_install_at(p->fds, 0, f);
    file_ref(f);
    fd_install_at(p->fds, 1, f);
    file_ref(f);
    fd_install_at(p->fds, 2, f);
}

/* Worker shared by create/spawn. Assumes the kernel CR3 is active. */
static struct process *create_full(const char *path, int argc, char *const argv[]) {
    struct process *parent = process_current();
    const char *cwd = parent ? parent->cwd : "/";

    struct user_address_space *space = user_address_space_create();
    if (!space) {
        return NULL;
    }
    uint64_t entry = 0;
    if (exec_load(cwd, path, space, &entry) != 0) {
        user_address_space_destroy(space);
        return NULL;
    }
    if (user_map_range(space, USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_SIZE,
                       PAGE_WRITABLE) != 0 ||
        user_map_range(space, USER_HEAP_BASE, USER_HEAP_INITIAL, PAGE_WRITABLE) != 0) {
        user_address_space_destroy(space);
        return NULL;
    }
    uint64_t rsp = build_user_stack(space, argc, argv);
    if (rsp == 0) {
        user_address_space_destroy(space);
        return NULL;
    }

    struct process *p = process_table_alloc();
    if (!p) {
        user_address_space_destroy(space);
        return NULL;
    }
    strlcpy(p->name, path_basename(path), sizeof(p->name));
    p->parent_pid = parent ? parent->pid : 0;
    p->state = PROCESS_NEW;
    p->exit_code = 0;
    p->address_space = space;
    p->entry_rip = entry;
    p->user_rsp = rsp;
    strlcpy(p->cwd, cwd, sizeof(p->cwd));

    p->fds = fd_table_create();
    if (!p->fds) {
        user_address_space_destroy(space);
        process_table_free(p);
        return NULL;
    }
    wire_stdio(p);
    return p;
}

struct process *process_create_argv(const char *path, int argc, char *const argv[]) {
    uint64_t saved = user_with_kernel_cr3();
    struct process *p = create_full(path, argc, argv);
    user_restore_cr3(saved);
    return p;
}

struct process *process_create(const char *path) {
    char *const argv[1] = { (char *)path };
    return process_create_argv(path, 1, argv);
}

/* Ring-0 trampoline: flip the freshly scheduled thread into ring 3. */
static void process_thread_entry(void *arg) {
    struct process *p = (struct process *)arg;
    p->state = PROCESS_RUNNING;
    user_enter_ring3(p->entry_rip, p->user_rsp,
                     user_address_space_cr3(p->address_space));
}

int process_spawn_argv(const char *path, int argc, char *const argv[]) {
    uint64_t saved = user_with_kernel_cr3();
    struct process *p = create_full(path, argc, argv);
    if (!p) {
        user_restore_cr3(saved);
        return -SYS_ENOENT;
    }
    struct thread *t = thread_create_raw(p->name, process_thread_entry, p);
    if (!t) {
        user_address_space_destroy(p->address_space);
        fd_table_destroy(p->fds);
        process_table_free(p);
        user_restore_cr3(saved);
        return -SYS_ENOMEM;
    }
    t->cr3 = user_address_space_cr3(p->address_space);
    t->proc = p;
    p->main_thread = t;
    p->state = PROCESS_READY;
    int pid = (int)p->pid;
    scheduler_make_ready(t);
    user_restore_cr3(saved);
    return pid;
}

int process_spawn(const char *path) {
    char *const argv[1] = { (char *)path };
    return process_spawn_argv(path, 1, argv);
}

void process_exit_current(int64_t code) {
    struct process *p = process_current();
    if (p) {
        p->state = PROCESS_EXITED;
        p->exit_code = code;
    }
    thread_exit();   /* marks the thread DEAD and reschedules; never returns */
    kernel_panic("process_exit_current resumed");
}

void process_fault_current(int64_t code) {
    struct process *p = process_current();
    if (p) {
        p->state = PROCESS_FAULTED;
        p->exit_code = code;
    }
    thread_exit();
    kernel_panic("process_fault_current resumed");
}

struct process *process_current(void) {
    struct thread *t = thread_current();
    return t ? (struct process *)t->proc : NULL;
}

uint64_t process_current_pid(void) {
    struct process *p = process_current();
    return p ? p->pid : 0;
}

struct process *process_by_pid(uint64_t pid) {
    return process_table_get(pid);
}

struct user_address_space *process_address_space(struct process *p) {
    return p ? p->address_space : NULL;
}

struct fd_table *process_fds(struct process *p) {
    return p ? p->fds : NULL;
}

char *process_cwd(struct process *p) {
    return p ? p->cwd : NULL;
}

int process_snapshot(struct sys_ps_entry *out, int max) {
    return process_table_snapshot(out, max);
}
