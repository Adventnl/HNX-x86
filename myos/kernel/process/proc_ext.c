/* Work Unit B: process-model expansion helpers — credentials, job control,
 * scheduling priority, the program break (brk/sbrk) over a pre-mapped arena,
 * a small environment block, and non-blocking zombie reaping. */
#include "process.h"
#include "process_table.h"
#include "user_address_space.h"
#include "user_copy.h"
#include "timer.h"
#include "string.h"
#include "syscall_numbers.h"
#include "kmath.h"

void process_init_ext(struct process *p) {
    p->uid = 0;
    p->gid = 0;
    p->euid = 0;
    p->egid = 0;
    p->pgid = p->pid;
    p->sid = p->pid;
    p->priority = 0;
    p->brk_base = PROCESS_BRK_BASE;
    p->brk_current = PROCESS_BRK_BASE;
    p->brk_max = PROCESS_BRK_BASE + PROCESS_BRK_INITIAL;
    p->mmap_next = PROCESS_MMAP_BASE;
    p->utime_ticks = 0;
    p->stime_ticks = 0;
    p->start_tick = kernel_ticks();
    p->child_count = 0;
    p->reaped = 0;
    p->sig_pending = 0;
    p->sig_blocked = 0;
    p->environ_len = 0;
    p->environ[0] = '\0';
}

int process_set_priority(struct process *p, int prio) {
    if (!p) {
        return -SYS_ESRCH;
    }
    if (prio < -20) {
        prio = -20;
    }
    if (prio > 19) {
        prio = 19;
    }
    p->priority = prio;
    return 0;
}

int process_get_priority(struct process *p) {
    return p ? p->priority : 0;
}

int process_setpgid(uint64_t pid, uint64_t pgid) {
    struct process *p = pid ? process_by_pid(pid) : process_current();
    if (!p) {
        return -SYS_ESRCH;
    }
    p->pgid = pgid ? pgid : p->pid;
    return 0;
}

uint64_t process_getpgid(uint64_t pid) {
    struct process *p = pid ? process_by_pid(pid) : process_current();
    return p ? p->pgid : 0;
}

uint64_t process_setsid(struct process *p) {
    if (!p) {
        return 0;
    }
    /* Become session + group leader. */
    p->sid = p->pid;
    p->pgid = p->pid;
    return p->sid;
}

/* ---- brk / sbrk over the pre-mapped arena -------------------------------- */

uint64_t process_brk(struct process *p, uint64_t new_break) {
    if (!p) {
        return 0;
    }
    if (new_break == 0) {
        return p->brk_current;       /* query */
    }
    if (new_break < p->brk_base) {
        return p->brk_current;       /* below the arena: reject, keep current */
    }
    if (new_break <= p->brk_max) {
        p->brk_current = new_break;
        return p->brk_current;
    }
    /* Growing beyond the pre-mapped arena: map more pages. The mapping touches
     * fresh frames through their identity addresses, reachable only under the
     * kernel CR3, so flip to it for the map window. */
    uint64_t want_end = align_up_u64(new_break, 0x1000);
    uint64_t add = want_end - p->brk_max;
    uint64_t saved = user_with_kernel_cr3();
    int rc = user_map_range(p->address_space, p->brk_max, add, 0x2 /*PAGE_WRITABLE*/);
    user_restore_cr3(saved);
    if (rc == 0) {
        p->brk_max = want_end;
        p->brk_current = new_break;
    }
    return p->brk_current;
}

uint64_t process_sbrk(struct process *p, int64_t delta) {
    if (!p) {
        return (uint64_t)-1;
    }
    uint64_t old = p->brk_current;
    if (delta == 0) {
        return old;
    }
    uint64_t target;
    if (delta < 0) {
        uint64_t dec = (uint64_t)(-delta);
        if (dec > old - p->brk_base) {
            target = p->brk_base;
        } else {
            target = old - dec;
        }
    } else {
        target = old + (uint64_t)delta;
    }
    uint64_t result = process_brk(p, target);
    if (delta > 0 && result < target) {
        return (uint64_t)-1;         /* could not grow */
    }
    return old;
}

/* ---- environment block --------------------------------------------------- */

int process_env_set(struct process *p, const char *kv) {
    if (!p || !kv) {
        return -SYS_EINVAL;
    }
    uint32_t len = (uint32_t)strlen(kv) + 1;
    if (p->environ_len + len > sizeof(p->environ)) {
        return -SYS_ENOMEM;
    }
    memcpy(p->environ + p->environ_len, kv, len);
    p->environ_len += len;
    return 0;
}

int process_env_get(struct process *p, const char *key, char *out, int max) {
    if (!p || !key) {
        return -SYS_EINVAL;
    }
    uint32_t klen = (uint32_t)strlen(key);
    uint32_t off = 0;
    while (off < p->environ_len) {
        const char *entry = p->environ + off;
        uint32_t elen = (uint32_t)strlen(entry);
        if (elen > klen && entry[klen] == '=' &&
            memcmp(entry, key, klen) == 0) {
            const char *val = entry + klen + 1;
            int vlen = (int)strlen(val);
            if (vlen + 1 > max) {
                return -SYS_ERANGE;
            }
            memcpy(out, val, (uint64_t)vlen + 1);
            return vlen;
        }
        off += elen + 1;
    }
    return -SYS_ENOENT;
}

/* ---- non-blocking zombie reaping ----------------------------------------- */

int process_reap_zombie(uint64_t pid, int64_t *exit_code, int nohang) {
    (void)nohang;
    struct process *self = process_current();
    uint64_t mypid = self ? self->pid : 0;

    /* Look for a matching exited (or faulted) child not yet reaped. */
    for (uint64_t scan = 1; scan <= PROCESS_MAX * 4; scan++) {
        struct process *c = process_by_pid(scan);
        if (!c) {
            continue;
        }
        if (c->parent_pid != mypid) {
            continue;
        }
        if (pid != 0 && c->pid != pid) {
            continue;
        }
        if ((c->state == PROCESS_EXITED || c->state == PROCESS_FAULTED) &&
            !c->reaped) {
            c->reaped = 1;
            if (exit_code) {
                *exit_code = c->exit_code;
            }
            return (int)c->pid;
        }
    }
    return 0;  /* no reapable child right now */
}
