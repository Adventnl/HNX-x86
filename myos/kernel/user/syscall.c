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
#include "driver_registry.h"
#include "driver.h"
#include "block_registry.h"
#include "usb.h"
#include "hid.h"
#include "hw_diag.h"
#include "irq.h"
#include "input_queue.h"
#include "input_event.h"
#include "mouse_event.h"
#include "pci.h"
#include "pci_device.h"
#include "msi.h"
#include "msix.h"
#include "pci_caps.h"
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

/* ---- namespace + storage introspection (Prompt 5) ------------------------ */
int64_t sys_mkdir(struct syscall_frame *f) {
    char *path = user_copy_string_from_user(f->rdi, VFS_PATH_MAX);
    if (!path) {
        return -SYS_EFAULT;
    }
    int64_t r = vfs_mkdir(path);
    kfree(path);
    return r;
}

int64_t sys_unlink(struct syscall_frame *f) {
    char *path = user_copy_string_from_user(f->rdi, VFS_PATH_MAX);
    if (!path) {
        return -SYS_EFAULT;
    }
    int64_t r = vfs_unlink(path);
    kfree(path);
    return r;
}

int64_t sys_stat(struct syscall_frame *f) {
    char *path = user_copy_string_from_user(f->rdi, VFS_PATH_MAX);
    if (!path) {
        return -SYS_EFAULT;
    }
    struct stat st;
    int r = vfs_stat(path, &st);
    kfree(path);
    if (r < 0) {
        return r;
    }
    struct sys_stat out;
    out.size = st.size;
    out.type = st.type;
    out.mode = st.mode;
    if (user_copy_to_user(f->rsi, &out, sizeof(out)) < 0) {
        return -SYS_EFAULT;
    }
    return 0;
}

int64_t sys_mount_info(struct syscall_frame *f) {
    int max = (int)f->rsi;
    if (max <= 0) {
        return 0;
    }
    int total = vfs_mount_count();
    if (max > total) {
        max = total;
    }
    for (int i = 0; i < max; i++) {
        struct sys_mount_entry e;
        memset(&e, 0, sizeof(e));
        vfs_mount_info(i, e.path, sizeof(e.path), e.fs, sizeof(e.fs));
        if (user_copy_to_user(f->rdi + (uint64_t)i * sizeof(e), &e, sizeof(e)) < 0) {
            return -SYS_EFAULT;
        }
    }
    return max;
}

int64_t sys_devices(struct syscall_frame *f) {
    int max = (int)f->rsi;
    if (max <= 0) {
        return 0;
    }
    int total = device_count();
    if (max > total) {
        max = total;
    }
    for (int i = 0; i < max; i++) {
        struct device *d = device_at(i);
        if (!d) {
            break;
        }
        struct sys_device_entry e;
        memset(&e, 0, sizeof(e));
        strlcpy(e.name, d->name, sizeof(e.name));
        e.type = (uint32_t)d->type;
        e.state = (uint32_t)d->state;
        e.power_state = (uint32_t)d->power_state;
        strlcpy(e.driver, d->driver ? d->driver->name : "", sizeof(e.driver));
        if (user_copy_to_user(f->rdi + (uint64_t)i * sizeof(e), &e, sizeof(e)) < 0) {
            return -SYS_EFAULT;
        }
    }
    return max;
}

int64_t sys_blocks(struct syscall_frame *f) {
    int max = (int)f->rsi;
    if (max <= 0) {
        return 0;
    }
    int total = block_device_count();
    if (max > total) {
        max = total;
    }
    for (int i = 0; i < max; i++) {
        struct block_device *b = block_device_at(i);
        if (!b) {
            break;
        }
        struct sys_block_entry e;
        memset(&e, 0, sizeof(e));
        strlcpy(e.name, b->name, sizeof(e.name));
        e.sectors = b->sector_count;
        e.sector_size = b->sector_size;
        if (user_copy_to_user(f->rdi + (uint64_t)i * sizeof(e), &e, sizeof(e)) < 0) {
            return -SYS_EFAULT;
        }
    }
    return max;
}

/* ---- Prompt 6: hardware / USB / input introspection --------------------- */

int64_t sys_usb_devices(struct syscall_frame *f) {
    int max = (int)f->rsi;
    if (max <= 0) {
        return 0;
    }
    int total = usb_device_count();
    if (max > total) {
        max = total;
    }
    for (int i = 0; i < max; i++) {
        struct usb_device *d = usb_device_at(i);
        if (!d) {
            break;
        }
        struct sys_usb_entry e;
        memset(&e, 0, sizeof(e));
        e.name[0] = 'u'; e.name[1] = 's'; e.name[2] = 'b';
        e.name[3] = (char)('0' + (d->hc_slot % 10)); e.name[4] = 0;
        e.vendor = d->vendor_id;
        e.product = d->product_id;
        e.dev_class = d->config.interface.iface_class;
        e.dev_subclass = d->config.interface.iface_subclass;
        e.dev_protocol = d->config.interface.iface_protocol;
        e.speed = d->speed;
        e.slot = d->hc_slot;
        if (d->config.interface.iface_class == 0x03) {
            e.hid_type = (d->config.interface.iface_protocol == 2) ? 2 : 1;
        }
        if (user_copy_to_user(f->rdi + (uint64_t)i * sizeof(e), &e, sizeof(e)) < 0) {
            return -SYS_EFAULT;
        }
    }
    return max;
}

int64_t sys_hw_info(struct syscall_frame *f) {
    struct hw_diag_summary s;
    hw_diag_collect(&s);
    struct sys_hw_info out;
    memset(&out, 0, sizeof(out));
    out.pci_functions = s.pci_functions;
    out.devices = s.devices;
    out.block_devices = s.block_devices;
    out.usb_devices = s.usb_devices;
    out.irq_vectors = s.irq_active_vectors;
    out.irq_total = s.irq_total;
    out.hw_events = s.hw_events;
    if (user_copy_to_user(f->rdi, &out, sizeof(out)) < 0) {
        return -SYS_EFAULT;
    }
    return 0;
}

int64_t sys_interrupts(struct syscall_frame *f) {
    int max = (int)f->rsi;
    if (max <= 0) {
        return 0;
    }
    int n = 0;
    for (uint16_t v = 0x20; v <= 0x4F && n < max; v++) {
        uint64_t c = irq_count_for_vector((uint8_t)v);
        if (!c) {
            continue;
        }
        struct sys_irq_entry e;
        memset(&e, 0, sizeof(e));
        e.vector = v;
        e.count = c;
        if (user_copy_to_user(f->rdi + (uint64_t)n * sizeof(e), &e, sizeof(e)) < 0) {
            return -SYS_EFAULT;
        }
        n++;
    }
    return n;
}

int64_t sys_input_poll(struct syscall_frame *f) {
    struct input_event ev;
    if (input_queue_pop(&ev) != 0) {
        return 0;                       /* none pending */
    }
    struct sys_input_event out;
    memset(&out, 0, sizeof(out));
    out.type = ev.type;
    out.code = ev.code;
    out.value = ev.value;
    out.value2 = ev.value2;
    out.source = ev.source;
    if (user_copy_to_user(f->rdi, &out, sizeof(out)) < 0) {
        return -SYS_EFAULT;
    }
    return 1;
}

int64_t sys_mouse_poll(struct syscall_frame *f) {
    struct mouse_event me;
    if (mouse_event_pop(&me) != 0) {
        return 0;
    }
    struct sys_mouse_event out;
    memset(&out, 0, sizeof(out));
    out.dx = me.dx;
    out.dy = me.dy;
    out.wheel = me.wheel;
    out.buttons = me.buttons;
    out.source = me.source;
    if (user_copy_to_user(f->rdi, &out, sizeof(out)) < 0) {
        return -SYS_EFAULT;
    }
    return 1;
}

int64_t sys_msi_info(struct syscall_frame *f) {
    int max = (int)f->rsi;
    if (max <= 0) {
        return 0;
    }
    int total = pci_device_count();
    if (max > total) {
        max = total;
    }
    for (int i = 0; i < max; i++) {
        struct pci_device *d = (struct pci_device *)pci_device_at(i);
        if (!d) {
            break;
        }
        struct sys_msi_entry e;
        memset(&e, 0, sizeof(e));
        e.name[0] = 'p'; e.name[1] = 'c'; e.name[2] = 'i';
        e.name[3] = (char)('0' + (d->slot % 10)); e.name[4] = 0;
        e.msi = (uint8_t)msi_supported(d);
        e.msix = (uint8_t)msix_supported(d);
        e.msix_count = e.msix ? (uint16_t)msix_table_size(d) : 0;
        e.vendor = d->vendor;
        e.device = d->device;
        if (user_copy_to_user(f->rdi + (uint64_t)i * sizeof(e), &e, sizeof(e)) < 0) {
            return -SYS_EFAULT;
        }
    }
    return max;
}

/* ---- dispatch ------------------------------------------------------------ */
int64_t syscall_dispatch(struct syscall_frame *frame) {
    syscall_fn fn = syscall_table_get(frame->rax);
    if (!fn) {
        return -SYS_ENOSYS;
    }
    return fn(frame);
}
