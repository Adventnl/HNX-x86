/* Shared user/kernel ABI structures for the structured syscalls (readdir, stat,
 * meminfo, ps). C-only — never included by assembly (crt0.S includes only
 * syscall_numbers.h). Layouts use fixed widths so the kernel and the freestanding
 * user runtime agree byte-for-byte. */
#ifndef MYOS_SYSCALL_ABI_H
#define MYOS_SYSCALL_ABI_H

struct sys_dirent {
    char               name[128];
    unsigned long long size;
    unsigned int       type;        /* 0=file, 1=dir, 2=chardev */
    unsigned int       _pad;
};

struct sys_stat {
    unsigned long long size;
    unsigned int       type;
    unsigned int       mode;
};

struct sys_meminfo {
    unsigned long long total_pages;
    unsigned long long free_pages;
    unsigned long long used_pages;
    unsigned long long page_size;
};

struct sys_ps_entry {
    unsigned long long pid;
    unsigned long long parent_pid;
    unsigned int       state;       /* enum process_state */
    unsigned int       _pad;
    char               name[32];
};

struct sys_mount_entry {
    char path[64];
    char fs[16];
};

struct sys_device_entry {
    char         name[32];
    unsigned int type;              /* enum device_type    */
    unsigned int state;             /* enum device_state   */
    unsigned int power_state;       /* enum device_power_state */
    char         driver[16];        /* bound driver, or "" */
};

/* ---- Prompt 6: hardware / USB / input introspection ---------------------- */

struct sys_usb_entry {
    char           name[16];
    unsigned short vendor;
    unsigned short product;
    unsigned char  dev_class;
    unsigned char  dev_subclass;
    unsigned char  dev_protocol;
    unsigned char  speed;
    unsigned char  slot;
    unsigned char  hid_type;        /* 0=none, 1=keyboard, 2=mouse */
    unsigned char  _pad[2];
};

struct sys_hw_info {
    unsigned int       pci_functions;
    unsigned int       devices;
    unsigned int       block_devices;
    unsigned int       usb_devices;
    unsigned int       irq_vectors;
    unsigned int       _pad;
    unsigned long long irq_total;
    unsigned long long hw_events;
};

struct sys_irq_entry {
    unsigned int       vector;
    unsigned int       _pad;
    unsigned long long count;
};

struct sys_input_event {
    unsigned short type;            /* enum input_event_type */
    unsigned short code;
    int            value;
    int            value2;
    unsigned short source;          /* enum input_source     */
    unsigned short _pad;
};

struct sys_mouse_event {
    short          dx;
    short          dy;
    signed char    wheel;
    unsigned char  buttons;
    unsigned short source;
};

struct sys_msi_entry {
    char           name[16];
    unsigned char  msi;             /* MSI capable    */
    unsigned char  msix;            /* MSI-X capable  */
    unsigned short msix_count;      /* MSI-X table size */
    unsigned short vendor;
    unsigned short device;
};

struct sys_block_entry {
    char               name[32];
    unsigned long long sectors;
    unsigned int       sector_size;
    unsigned int       _pad;
};

/* ---- Work Unit B: time ABI ----------------------------------------------- */

struct sys_timeval {
    unsigned long long tv_sec;
    unsigned long long tv_usec;
};

struct sys_timespec {
    unsigned long long tv_sec;
    unsigned long long tv_nsec;
};

#endif /* MYOS_SYSCALL_ABI_H */
