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

#endif /* MYOS_SYSCALL_ABI_H */
