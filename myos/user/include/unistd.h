/* POSIX-flavored syscall wrappers (MyOS is not POSIX-compatible; the names just
 * ease future expansion). */
#ifndef MYOS_USER_UNISTD_H
#define MYOS_USER_UNISTD_H

#include "types.h"
#include "syscall_abi.h"

long write(int fd, const void *buf, unsigned long n);
long read(int fd, void *buf, unsigned long n);
int  close(int fd);
long lseek(int fd, long offset, int whence);
int  readdir(int fd, struct sys_dirent *out);     /* 1 = entry, 0 = end, <0 err */

long getpid(void);
long yield(void);
long sleep_ms(unsigned long ms);

long spawn(const char *path, char *const argv[]);
long wait_pid(long pid, long *exit_code);

long getcwd(char *buf, unsigned long size);
int  chdir(const char *path);

unsigned long uptime_ms(void);
int meminfo(struct sys_meminfo *out);
int ps(struct sys_ps_entry *out, int max);

/* Prompt 5: namespace mutation + storage introspection. */
int mkdir(const char *path);
int unlink(const char *path);
int stat(const char *path, struct sys_stat *out);
int mounts(struct sys_mount_entry *out, int max);
int devices(struct sys_device_entry *out, int max);
int blocks(struct sys_block_entry *out, int max);

#endif /* MYOS_USER_UNISTD_H */
