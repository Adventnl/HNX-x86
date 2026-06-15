/* Process model v0: a PID-bearing object owning a user address space, a main
 * kernel thread, a file-descriptor table and a working directory. One thread per
 * process (no fork; spawn-by-path + wait + exit). */
#ifndef MYOS_PROCESS_H
#define MYOS_PROCESS_H

#include "types.h"

struct user_address_space;
struct thread;
struct fd_table;
struct sys_ps_entry;

enum process_state {
    PROCESS_NEW,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_SLEEPING,
    PROCESS_WAITING,
    PROCESS_EXITED,
    PROCESS_FAULTED,
};

#define PROCESS_NAME_MAX 32
#define PROCESS_CWD_MAX  256
#define PROCESS_MAX_ARGS 32

struct process {
    uint64_t            pid;
    uint64_t            parent_pid;
    char                name[PROCESS_NAME_MAX];
    enum process_state  state;
    int64_t             exit_code;

    struct user_address_space *address_space;
    struct thread             *main_thread;
    struct fd_table           *fds;

    char     cwd[PROCESS_CWD_MAX];

    uint64_t entry_rip;     /* user entry point */
    uint64_t user_rsp;      /* initial user stack pointer (argv block built) */

    /* ---- Work Unit B: process model expansion ---- */
    /* Credentials (foundation; no enforcement yet). */
    uint32_t uid, gid;
    uint32_t euid, egid;

    /* Job control: process group + session. */
    uint64_t pgid;
    uint64_t sid;

    /* Scheduling. */
    int32_t  priority;      /* -20..19 nice-style; 0 = default */

    /* Program break (brk/sbrk) over a pre-mapped arena. */
    uint64_t brk_base;
    uint64_t brk_current;
    uint64_t brk_max;

    /* mmap foundation: next anonymous mapping hint. */
    uint64_t mmap_next;

    /* CPU time accounting (ticks). */
    uint64_t utime_ticks;
    uint64_t stime_ticks;
    uint64_t start_tick;

    /* Lifecycle bookkeeping. */
    uint64_t child_count;
    int      reaped;        /* set once a zombie has been waited on */

    /* Signal foundation (no delivery yet): pending + blocked masks. */
    uint64_t sig_pending;
    uint64_t sig_blocked;

    /* Small environment block (KEY=VALUE strings, NUL-separated). */
    char     environ[512];
    uint32_t environ_len;
};

/* ---- Work Unit B accessors / helpers (kernel/process/proc_ext.c) ---- */
void     process_init_ext(struct process *p);     /* defaults for new fields */
int      process_set_priority(struct process *p, int prio);
int      process_get_priority(struct process *p);
int      process_setpgid(uint64_t pid, uint64_t pgid);
uint64_t process_getpgid(uint64_t pid);
uint64_t process_setsid(struct process *p);
/* brk/sbrk over the per-process pre-mapped arena. */
uint64_t process_brk(struct process *p, uint64_t new_break);  /* returns cur break */
uint64_t process_sbrk(struct process *p, int64_t delta);      /* old break or -1 */
/* Environment block helpers. */
int      process_env_set(struct process *p, const char *kv);
int      process_env_get(struct process *p, const char *key, char *out, int max);
/* Non-blocking reap of any/specific exited child; returns pid or -ECHILD/0. */
int      process_reap_zombie(uint64_t pid, int64_t *exit_code, int nohang);

#define PROCESS_BRK_BASE     0x0000005000000000ULL
#define PROCESS_BRK_INITIAL  0x0000000000040000ULL  /* 256 KiB pre-mapped */
#define PROCESS_MMAP_BASE    0x0000006000000000ULL

void process_system_init(void);

/* Build a process from an HXE1 path (argv defaults to { path }). Loads the
 * image, builds the address space/stack/heap and fd table. State NEW; not yet
 * scheduled. Must run with the kernel CR3 active. NULL on failure. */
struct process *process_create(const char *path);
struct process *process_create_argv(const char *path, int argc, char *const argv[]);

/* Create + admit to the scheduler. Returns the new pid or a negative error.
 * Switches to the kernel CR3 for the (non-blocking) creation window. */
int process_spawn(const char *path);
int process_spawn_argv(const char *path, int argc, char *const argv[]);

/* Block the caller until child `pid` terminates, reap it, return pid (or err).
 * Stores the exit code in *exit_code (kernel pointer) when non-NULL. */
int process_wait(uint64_t pid, int64_t *exit_code);

void process_exit_current(int64_t code) __attribute__((noreturn));
void process_fault_current(int64_t code) __attribute__((noreturn));

struct process *process_current(void);
uint64_t        process_current_pid(void);
struct process *process_by_pid(uint64_t pid);

/* Accessors. */
struct user_address_space *process_address_space(struct process *p);
struct fd_table           *process_fds(struct process *p);
char                      *process_cwd(struct process *p);

/* Fill up to `max` ps entries; returns the count written. */
int process_snapshot(struct sys_ps_entry *out, int max);

#endif /* MYOS_PROCESS_H */
