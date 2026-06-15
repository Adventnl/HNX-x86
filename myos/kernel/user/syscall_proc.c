/* Work Unit B: process / credentials / time / fd / memory syscall handlers.
 * Kept separate from the Prompt 4/5/6 handlers in syscall.c. Every handler
 * validates user pointers via user_copy_* and returns negative errno on error. */
#include "syscall.h"
#include "syscall_table.h"
#include "syscall_abi.h"
#include "process.h"
#include "process_table.h"
#include "fd_table.h"
#include "file.h"
#include "thread.h"
#include "user_copy.h"
#include "user_address_space.h"
#include "user.h"
#include "sleep.h"
#include "scheduler.h"
#include "timer.h"
#include "memory_layout.h"
#include "string.h"
#include "syscall_numbers.h"

/* ---- identity / credentials ---------------------------------------------- */

int64_t sys_getppid(struct syscall_frame *f) {
    (void)f;
    struct process *p = process_current();
    return p ? (int64_t)p->parent_pid : 0;
}

int64_t sys_gettid(struct syscall_frame *f) {
    (void)f;
    return (int64_t)thread_current_id();
}

int64_t sys_getuid(struct syscall_frame *f) {
    (void)f;
    struct process *p = process_current();
    return p ? (int64_t)p->uid : 0;
}

int64_t sys_setuid(struct syscall_frame *f) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    /* Foundation: only the (uid 0) root process may change uid. */
    if (p->euid != 0 && (uint32_t)f->rdi != p->uid) {
        return -SYS_EPERM;
    }
    p->uid = (uint32_t)f->rdi;
    p->euid = (uint32_t)f->rdi;
    return 0;
}

int64_t sys_getgid(struct syscall_frame *f) {
    (void)f;
    struct process *p = process_current();
    return p ? (int64_t)p->gid : 0;
}

int64_t sys_setgid(struct syscall_frame *f) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    if (p->euid != 0 && (uint32_t)f->rdi != p->gid) {
        return -SYS_EPERM;
    }
    p->gid = (uint32_t)f->rdi;
    p->egid = (uint32_t)f->rdi;
    return 0;
}

int64_t sys_getpriority(struct syscall_frame *f) {
    uint64_t pid = f->rsi;     /* who; 0 => self */
    struct process *p = pid ? process_by_pid(pid) : process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    return process_get_priority(p);
}

int64_t sys_setpriority(struct syscall_frame *f) {
    uint64_t pid = f->rsi;
    int prio = (int)(int64_t)f->rdx;
    struct process *p = pid ? process_by_pid(pid) : process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    return process_set_priority(p, prio);
}

/* ---- job control --------------------------------------------------------- */

int64_t sys_getpgid(struct syscall_frame *f) {
    return (int64_t)process_getpgid(f->rdi);
}
int64_t sys_setpgid(struct syscall_frame *f) {
    return process_setpgid(f->rdi, f->rsi);
}
int64_t sys_getsid(struct syscall_frame *f) {
    struct process *p = f->rdi ? process_by_pid(f->rdi) : process_current();
    return p ? (int64_t)p->sid : -SYS_ESRCH;
}
int64_t sys_setsid(struct syscall_frame *f) {
    (void)f;
    struct process *p = process_current();
    return p ? (int64_t)process_setsid(p) : -SYS_ESRCH;
}

/* ---- memory: brk / sbrk / mmap / munmap ---------------------------------- */

int64_t sys_brk(struct syscall_frame *f) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    return (int64_t)process_brk(p, f->rdi);
}

int64_t sys_sbrk(struct syscall_frame *f) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    uint64_t old = process_sbrk(p, (int64_t)f->rdi);
    if (old == (uint64_t)-1) {
        return -SYS_ENOMEM;
    }
    return (int64_t)old;
}

int64_t sys_mmap(struct syscall_frame *f) {
    /* Anonymous private mapping foundation: ignore fd/offset, map fresh zeroed
     * user pages. rdi=addr hint, rsi=length, rdx=prot, r10=flags. */
    struct process *p = process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    uint64_t length = f->rsi;
    if (length == 0) {
        return -SYS_EINVAL;
    }
    length = PAGE_ALIGN_UP(length);
    uint64_t prot = f->rdx;
    uint64_t pflags = (prot & PROT_WRITE) ? 0x2 /*PAGE_WRITABLE*/ : 0;

    uint64_t addr = f->rdi ? PAGE_ALIGN_DOWN(f->rdi) : p->mmap_next;
    /* Mapping allocates page-table + data frames and zeroes them through their
     * identity addresses, which are only reachable under the kernel CR3. */
    uint64_t saved = user_with_kernel_cr3();
    int rc = user_map_range(p->address_space, addr, length, pflags);
    user_restore_cr3(saved);
    if (rc != 0) {
        return -SYS_ENOMEM;
    }
    if (!f->rdi) {
        p->mmap_next = addr + length;
    }
    return (int64_t)addr;
}

int64_t sys_munmap(struct syscall_frame *f) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    uint64_t addr = f->rdi;
    uint64_t length = PAGE_ALIGN_UP(f->rsi);
    if (length == 0 || (addr & PAGE_MASK)) {
        return -SYS_EINVAL;
    }
    uint64_t saved = user_with_kernel_cr3();
    int rc = user_unmap_range(p->address_space, addr, length);
    user_restore_cr3(saved);
    if (rc != 0) {
        return -SYS_EINVAL;
    }
    return 0;
}

/* ---- file descriptors: dup / dup2 / fcntl / ioctl / pipe ----------------- */

int64_t sys_dup(struct syscall_frame *f) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    struct file *file = fd_get(p->fds, (int)f->rdi);
    if (!file) {
        return -SYS_EBADF;
    }
    file_ref(file);
    int nfd = fd_alloc(p->fds, file);
    if (nfd < 0) {
        file_unref(file);
        return -SYS_EMFILE;
    }
    return nfd;
}

int64_t sys_dup2(struct syscall_frame *f) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    int oldfd = (int)f->rdi;
    int newfd = (int)f->rsi;
    struct file *file = fd_get(p->fds, oldfd);
    if (!file) {
        return -SYS_EBADF;
    }
    if (oldfd == newfd) {
        return newfd;
    }
    if (newfd < 0 || newfd >= FD_MAX) {
        return -SYS_EBADF;
    }
    file_ref(file);
    fd_install_at(p->fds, newfd, file);   /* closes any current occupant */
    return newfd;
}

int64_t sys_fcntl(struct syscall_frame *f) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    int fd = (int)f->rdi;
    int cmd = (int)f->rsi;
    struct file *file = fd_get(p->fds, fd);
    if (!file) {
        return -SYS_EBADF;
    }
    switch (cmd) {
    case F_DUPFD: {
        file_ref(file);
        int nfd = fd_alloc(p->fds, file);
        if (nfd < 0) {
            file_unref(file);
            return -SYS_EMFILE;
        }
        return nfd;
    }
    case F_GETFD:
        return 0;                 /* no per-fd flags tracked yet */
    case F_SETFD:
        return 0;                 /* FD_CLOEXEC accepted, not yet enforced */
    case F_GETFL:
        return file->flags;
    case F_SETFL:
        file->flags = (int)f->rdx;
        return 0;
    default:
        return -SYS_EINVAL;
    }
}

int64_t sys_ioctl(struct syscall_frame *f) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    struct file *file = fd_get(p->fds, (int)f->rdi);
    if (!file) {
        return -SYS_EBADF;
    }
    /* ioctl foundation: no device ioctls wired yet. */
    return -SYS_ENOSYS;
}

int64_t sys_pipe(struct syscall_frame *f) {
    (void)f;
    /* pipe foundation: the pipe object/vnode is a later milestone. */
    return -SYS_ENOSYS;
}

/* ---- wait / kill --------------------------------------------------------- */

static int has_children(uint64_t mypid) {
    for (uint64_t scan = 1; scan <= PROCESS_MAX * 4; scan++) {
        struct process *c = process_by_pid(scan);
        if (c && c->parent_pid == mypid && !c->reaped) {
            return 1;
        }
    }
    return 0;
}

int64_t sys_waitpid(struct syscall_frame *f) {
    uint64_t pid = f->rdi;
    uint64_t ustatus = f->rsi;
    uint64_t options = f->rdx;
    struct process *self = process_current();
    uint64_t mypid = self ? self->pid : 0;

    int64_t code = 0;

    /* Specific child, blocking, no WNOHANG: use the existing blocking path. */
    if (pid != 0 && pid != (uint64_t)-1 && !(options & WNOHANG)) {
        int r = process_wait(pid, &code);
        if (r < 0) {
            return r;
        }
        if (ustatus && user_copy_to_user(ustatus, &code, sizeof(code)) < 0) {
            return -SYS_EFAULT;
        }
        return r;
    }

    /* Any-child or WNOHANG: poll for a reapable zombie. */
    uint64_t want = (pid == (uint64_t)-1) ? 0 : pid;
    for (int spins = 0; spins < 2000; spins++) {
        int r = process_reap_zombie(want, &code, 1);
        if (r > 0) {
            if (ustatus && user_copy_to_user(ustatus, &code, sizeof(code)) < 0) {
                return -SYS_EFAULT;
            }
            return r;
        }
        if (!has_children(mypid)) {
            return -SYS_ECHILD;
        }
        if (options & WNOHANG) {
            return 0;
        }
        thread_sleep_ms(2);
    }
    return -SYS_ECHILD;
}

int64_t sys_kill(struct syscall_frame *f) {
    uint64_t pid = f->rdi;
    int sig = (int)f->rsi;
    struct process *p = process_by_pid(pid);
    if (!p) {
        return -SYS_ESRCH;
    }
    if (sig < 0 || sig > 63) {
        return -SYS_EINVAL;
    }
    /* kill placeholder: record the pending signal; no delivery yet. */
    if (sig > 0) {
        p->sig_pending |= (1ULL << sig);
    }
    return 0;
}

/* ---- time ---------------------------------------------------------------- */

int64_t sys_gettimeofday(struct syscall_frame *f) {
    uint64_t ms = kernel_uptime_ms();
    struct sys_timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    if (f->rdi && user_copy_to_user(f->rdi, &tv, sizeof(tv)) < 0) {
        return -SYS_EFAULT;
    }
    return 0;
}

int64_t sys_clock_gettime(struct syscall_frame *f) {
    uint64_t clockid = f->rdi;
    (void)clockid;   /* both clocks tied to the monotonic tick for now */
    uint64_t ms = kernel_uptime_ms();
    struct sys_timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000ULL;
    if (f->rsi && user_copy_to_user(f->rsi, &ts, sizeof(ts)) < 0) {
        return -SYS_EFAULT;
    }
    return 0;
}

int64_t sys_nanosleep(struct syscall_frame *f) {
    struct sys_timespec ts;
    if (!f->rdi || user_copy_from_user(&ts, f->rdi, sizeof(ts)) < 0) {
        return -SYS_EFAULT;
    }
    uint64_t ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000ULL;
    thread_sleep_ms(ms);
    return 0;
}

/* ---- environment --------------------------------------------------------- */

int64_t sys_env_set(struct syscall_frame *f) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    char *kv = user_copy_string_from_user(f->rdi, 256);
    if (!kv) {
        return -SYS_EFAULT;
    }
    int r = process_env_set(p, kv);
    extern void kfree(void *);
    kfree(kv);
    return r;
}

int64_t sys_env_get(struct syscall_frame *f) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    char *key = user_copy_string_from_user(f->rdi, 128);
    if (!key) {
        return -SYS_EFAULT;
    }
    char val[256];
    int r = process_env_get(p, key, val, sizeof(val));
    extern void kfree(void *);
    kfree(key);
    if (r < 0) {
        return r;
    }
    if (f->rsi && user_copy_to_user(f->rsi, val, (uint64_t)r + 1) < 0) {
        return -SYS_EFAULT;
    }
    return r;
}
