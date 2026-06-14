/* System-call dispatcher + handlers.
 *
 * Every handler validates user pointers in software (the active CR3 belongs to
 * the caller, so a bad pointer must never be dereferenced — that would fault in
 * ring 0 and be misreported). Bad input returns a negative errno; nothing here
 * panics on user error. */
#include "syscall.h"
#include "syscall_table.h"
#include "syscall_abi.h"
#include "user.h"
#include "user_copy.h"
#include "process.h"
#include "process_table.h"
#include "vfs.h"
#include "path.h"
#include "inode.h"
#include "idt.h"
#include "gdt.h"
#include "scheduler.h"
#include "sleep.h"
#include "timer.h"
#include "pmm.h"
#include "memory_layout.h"
#include "heap.h"
#include "string.h"
#include "log.h"

void syscall_init(void) {
    idt_set_gate(SYSCALL_VECTOR, (void *)syscall_entry,
                 GDT_KERNEL_CODE, IDT_FLAG_INTERRUPT_GATE_DPL3);
    kernel_log_ok("Syscall vector installed");
    kernel_log_ok("Syscall dispatcher online");
}

/* ---- process control ----------------------------------------------------- */
int64_t sys_exit(struct syscall_frame *f) {
    process_exit_current((int64_t)f->rdi);   /* never returns */
    return 0;
}

int64_t sys_getpid(struct syscall_frame *f) {
    (void)f;
    return (int64_t)process_current_pid();
}

int64_t sys_yield(struct syscall_frame *f) {
    (void)f;
    scheduler_yield();
    return 0;
}

int64_t sys_sleep(struct syscall_frame *f) {
    thread_sleep_ms(f->rdi);
    return 0;
}

/* ---- I/O over the fd table ----------------------------------------------- */
int64_t sys_write(struct syscall_frame *f) {
    uint64_t fd = f->rdi, ubuf = f->rsi, len = f->rdx;
    if (len == 0) {
        return 0;
    }
    char chunk[256];
    uint64_t done = 0;
    while (done < len) {
        uint64_t n = len - done;
        if (n > sizeof(chunk)) {
            n = sizeof(chunk);
        }
        if (user_copy_from_user(chunk, ubuf + done, n) < 0) {
            return -SYS_EFAULT;
        }
        int64_t w = vfs_write((int)fd, chunk, n);
        if (w < 0) {
            return (done > 0) ? (int64_t)done : w;
        }
        done += (uint64_t)w;
        if ((uint64_t)w < n) {
            break;
        }
    }
    return (int64_t)done;
}

int64_t sys_read(struct syscall_frame *f) {
    uint64_t fd = f->rdi, ubuf = f->rsi, len = f->rdx;
    if (len == 0) {
        return 0;
    }
    char chunk[256];
    uint64_t n = (len < sizeof(chunk)) ? len : sizeof(chunk);
    int64_t r = vfs_read((int)fd, chunk, n);
    if (r <= 0) {
        return r;   /* error or EOF(0) */
    }
    if (user_copy_to_user(ubuf, chunk, (uint64_t)r) < 0) {
        return -SYS_EFAULT;
    }
    return r;
}

int64_t sys_open(struct syscall_frame *f) {
    char *path = user_copy_string_from_user(f->rdi, VFS_PATH_MAX);
    if (!path) {
        return -SYS_EFAULT;
    }
    int64_t fd = vfs_open(path, (int)f->rsi);
    kfree(path);
    return fd;
}

int64_t sys_close(struct syscall_frame *f) {
    return vfs_close((int)f->rdi);
}

int64_t sys_lseek(struct syscall_frame *f) {
    return vfs_lseek((int)f->rdi, (int64_t)f->rsi, (int)f->rdx);
}

int64_t sys_readdir(struct syscall_frame *f) {
    struct dirent kd;
    int r = vfs_readdir((int)f->rdi, &kd);
    if (r <= 0) {
        return r;   /* 0 = end, <0 = error */
    }
    struct sys_dirent sd;
    memset(&sd, 0, sizeof(sd));
    strlcpy(sd.name, kd.name, sizeof(sd.name));
    sd.size = kd.size;
    sd.type = kd.type;
    if (user_copy_to_user(f->rsi, &sd, sizeof(sd)) < 0) {
        return -SYS_EFAULT;
    }
    return 1;
}

/* ---- process creation ---------------------------------------------------- */
int64_t sys_spawn(struct syscall_frame *f) {
    char *path = user_copy_string_from_user(f->rdi, VFS_PATH_MAX);
    if (!path) {
        return -SYS_EFAULT;
    }
    char *argv[PROCESS_MAX_ARGS];
    int argc = 0;
    uint64_t uargv = f->rsi;
    if (uargv) {
        while (argc < PROCESS_MAX_ARGS) {
            uint64_t ptr = 0;
            if (user_copy_from_user(&ptr, uargv + (uint64_t)argc * 8, 8) < 0) {
                kfree(path);
                return -SYS_EFAULT;
            }
            if (ptr == 0) {
                break;
            }
            char *s = user_copy_string_from_user(ptr, 256);
            if (!s) {
                kfree(path);
                return -SYS_EFAULT;
            }
            argv[argc++] = s;
        }
    }
    if (argc == 0) {
        argv[0] = path;
        argc = 1;
    }
    int64_t pid = process_spawn_argv(path, argc, argv);
    for (int i = 0; i < argc; i++) {
        if (argv[i] != path) {
            kfree(argv[i]);
        }
    }
    kfree(path);
    return pid;
}

int64_t sys_wait(struct syscall_frame *f) {
    int64_t code = 0;
    int r = process_wait(f->rdi, &code);
    if (r < 0) {
        return r;
    }
    if (f->rsi) {
        if (user_copy_to_user(f->rsi, &code, sizeof(code)) < 0) {
            return -SYS_EFAULT;
        }
    }
    return r;
}

/* ---- working directory --------------------------------------------------- */
int64_t sys_getcwd(struct syscall_frame *f) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_EFAULT;
    }
    const char *cwd = process_cwd(p);
    uint64_t need = strlen(cwd) + 1;
    if (f->rsi < need) {
        return -SYS_ERANGE;
    }
    if (user_copy_to_user(f->rdi, cwd, need) < 0) {
        return -SYS_EFAULT;
    }
    return (int64_t)(need - 1);
}

int64_t sys_chdir(struct syscall_frame *f) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_EFAULT;
    }
    char *path = user_copy_string_from_user(f->rdi, VFS_PATH_MAX);
    if (!path) {
        return -SYS_EFAULT;
    }
    char abs[VFS_PATH_MAX];
    int r = path_resolve(process_cwd(p), path, abs, sizeof(abs));
    kfree(path);
    if (r < 0) {
        return r;
    }
    struct vnode *vn = vfs_resolve(abs);
    if (!vn) {
        return -SYS_ENOENT;
    }
    if (vn->type != VNODE_DIR) {
        return -SYS_ENOTDIR;
    }
    strlcpy(p->cwd, abs, sizeof(p->cwd));
    return 0;
}

/* ---- introspection ------------------------------------------------------- */
int64_t sys_uptime(struct syscall_frame *f) {
    (void)f;
    return (int64_t)kernel_uptime_ms();
}

int64_t sys_meminfo(struct syscall_frame *f) {
    struct sys_meminfo m;
    m.total_pages = pmm_total_pages();
    m.free_pages = pmm_free_pages();
    m.used_pages = pmm_used_pages();
    m.page_size = PAGE_SIZE;
    if (user_copy_to_user(f->rdi, &m, sizeof(m)) < 0) {
        return -SYS_EFAULT;
    }
    return 0;
}

int64_t sys_ps(struct syscall_frame *f) {
    int max = (int)f->rsi;
    if (max <= 0) {
        return 0;
    }
    struct sys_ps_entry tmp[PROCESS_MAX];
    if (max > PROCESS_MAX) {
        max = PROCESS_MAX;
    }
    int n = process_snapshot(tmp, max);
    if (n > 0) {
        if (user_copy_to_user(f->rdi, tmp, (uint64_t)n * sizeof(tmp[0])) < 0) {
            return -SYS_EFAULT;
        }
    }
    return n;
}

/* ---- dispatch ------------------------------------------------------------ */
int64_t syscall_dispatch(struct syscall_frame *frame) {
    syscall_fn fn = syscall_table_get(frame->rax);
    if (!fn) {
        return -SYS_ENOSYS;
    }
    return fn(frame);
}
