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

/* Prompt 6: hardware / USB / input introspection. */
int usb_devices(struct sys_usb_entry *out, int max);
int hw_info(struct sys_hw_info *out);
int interrupts(struct sys_irq_entry *out, int max);
int input_poll(struct sys_input_event *out);     /* 1 = event, 0 = none */
int mouse_poll(struct sys_mouse_event *out);      /* 1 = event, 0 = none */
int msi_info(struct sys_msi_entry *out, int max);

/* ---- Work Unit B: process / credentials / time / fd / memory ---- */
long getppid(void);
long gettid(void);
long getuid(void);
long setuid(long uid);
long getgid(void);
long setgid(long gid);
int  getpriority(int which, long who);
int  setpriority(int which, long who, int prio);

long  brk(unsigned long addr);
void *sbrk(long delta);
void *mmap(void *addr, unsigned long len, int prot, int flags);
int   munmap(void *addr, unsigned long len);

int dup(int fd);
int dup2(int oldfd, int newfd);
int fcntl(int fd, int cmd, long arg);
int ioctl(int fd, unsigned long req, long arg);
int pipe(int fds[2]);

long waitpid(long pid, long *status, int options);
int  kill(long pid, int sig);

int gettimeofday(struct sys_timeval *tv);
int clock_gettime(int clk, struct sys_timespec *ts);
int nanosleep(const struct sys_timespec *ts);

long getpgid(long pid);
int  setpgid(long pid, long pgid);
long getsid(long pid);
long setsid(void);

int env_set(const char *kv);
int env_get(const char *key, char *out);

#endif /* MYOS_USER_UNISTD_H */
