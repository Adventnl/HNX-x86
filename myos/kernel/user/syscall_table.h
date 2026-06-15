/* Static syscall table: maps a syscall number to its handler. Handlers live in
 * syscall.c and take the decoded syscall frame; the table is defined in
 * syscall_table.c. */
#ifndef MYOS_SYSCALL_TABLE_H
#define MYOS_SYSCALL_TABLE_H

#include "types.h"

struct syscall_frame;

typedef int64_t (*syscall_fn)(struct syscall_frame *f);

/* Returns the handler for `nr`, or NULL if out of range / unimplemented. */
syscall_fn syscall_table_get(uint64_t nr);

/* Handler prototypes (implemented in syscall.c). */
int64_t sys_exit(struct syscall_frame *f);
int64_t sys_write(struct syscall_frame *f);
int64_t sys_read(struct syscall_frame *f);
int64_t sys_sleep(struct syscall_frame *f);
int64_t sys_getpid(struct syscall_frame *f);
int64_t sys_yield(struct syscall_frame *f);
int64_t sys_open(struct syscall_frame *f);
int64_t sys_close(struct syscall_frame *f);
int64_t sys_lseek(struct syscall_frame *f);
int64_t sys_readdir(struct syscall_frame *f);
int64_t sys_spawn(struct syscall_frame *f);
int64_t sys_wait(struct syscall_frame *f);
int64_t sys_getcwd(struct syscall_frame *f);
int64_t sys_chdir(struct syscall_frame *f);
int64_t sys_uptime(struct syscall_frame *f);
int64_t sys_meminfo(struct syscall_frame *f);
int64_t sys_ps(struct syscall_frame *f);
int64_t sys_mkdir(struct syscall_frame *f);
int64_t sys_unlink(struct syscall_frame *f);
int64_t sys_stat(struct syscall_frame *f);
int64_t sys_mount_info(struct syscall_frame *f);
int64_t sys_devices(struct syscall_frame *f);
int64_t sys_blocks(struct syscall_frame *f);
int64_t sys_usb_devices(struct syscall_frame *f);
int64_t sys_hw_info(struct syscall_frame *f);
int64_t sys_interrupts(struct syscall_frame *f);
int64_t sys_input_poll(struct syscall_frame *f);
int64_t sys_mouse_poll(struct syscall_frame *f);
int64_t sys_msi_info(struct syscall_frame *f);

#endif /* MYOS_SYSCALL_TABLE_H */
