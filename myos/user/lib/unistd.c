/* Named syscall wrappers. */
#include "unistd.h"
#include "fcntl.h"
#include "syscall.h"

long write(int fd, const void *buf, unsigned long n) {
    return __syscall(SYS_WRITE, fd, (long)buf, (long)n);
}

long read(int fd, void *buf, unsigned long n) {
    return __syscall(SYS_READ, fd, (long)buf, (long)n);
}

int open(const char *path, int flags) {
    return (int)__syscall(SYS_OPEN, (long)path, flags, 0);
}

int close(int fd) {
    return (int)__syscall(SYS_CLOSE, fd, 0, 0);
}

long lseek(int fd, long offset, int whence) {
    return __syscall(SYS_LSEEK, fd, offset, whence);
}

int readdir(int fd, struct sys_dirent *out) {
    return (int)__syscall(SYS_READDIR, fd, (long)out, 0);
}

long getpid(void) {
    return __syscall(SYS_GETPID, 0, 0, 0);
}

long yield(void) {
    return __syscall(SYS_YIELD, 0, 0, 0);
}

long sleep_ms(unsigned long ms) {
    return __syscall(SYS_SLEEP, (long)ms, 0, 0);
}

long spawn(const char *path, char *const argv[]) {
    return __syscall(SYS_SPAWN, (long)path, (long)argv, 0);
}

long wait_pid(long pid, long *exit_code) {
    return __syscall(SYS_WAIT, pid, (long)exit_code, 0);
}

long getcwd(char *buf, unsigned long size) {
    return __syscall(SYS_GETCWD, (long)buf, (long)size, 0);
}

int chdir(const char *path) {
    return (int)__syscall(SYS_CHDIR, (long)path, 0, 0);
}

unsigned long uptime_ms(void) {
    return (unsigned long)__syscall(SYS_UPTIME, 0, 0, 0);
}

int meminfo(struct sys_meminfo *out) {
    return (int)__syscall(SYS_MEMINFO, (long)out, 0, 0);
}

int ps(struct sys_ps_entry *out, int max) {
    return (int)__syscall(SYS_PS, (long)out, max, 0);
}

int mkdir(const char *path) {
    return (int)__syscall(SYS_MKDIR, (long)path, 0, 0);
}

int unlink(const char *path) {
    return (int)__syscall(SYS_UNLINK, (long)path, 0, 0);
}

int stat(const char *path, struct sys_stat *out) {
    return (int)__syscall(SYS_STAT, (long)path, (long)out, 0);
}

int mounts(struct sys_mount_entry *out, int max) {
    return (int)__syscall(SYS_MOUNT_INFO, (long)out, max, 0);
}

int devices(struct sys_device_entry *out, int max) {
    return (int)__syscall(SYS_DEVICES, (long)out, max, 0);
}

int blocks(struct sys_block_entry *out, int max) {
    return (int)__syscall(SYS_BLOCKS, (long)out, max, 0);
}

int usb_devices(struct sys_usb_entry *out, int max) {
    return (int)__syscall(SYS_USB_DEVICES, (long)out, max, 0);
}

int hw_info(struct sys_hw_info *out) {
    return (int)__syscall(SYS_HW_INFO, (long)out, 0, 0);
}

int interrupts(struct sys_irq_entry *out, int max) {
    return (int)__syscall(SYS_INTERRUPTS, (long)out, max, 0);
}

int input_poll(struct sys_input_event *out) {
    return (int)__syscall(SYS_INPUT_POLL, (long)out, 0, 0);
}

int mouse_poll(struct sys_mouse_event *out) {
    return (int)__syscall(SYS_MOUSE_POLL, (long)out, 0, 0);
}

int msi_info(struct sys_msi_entry *out, int max) {
    return (int)__syscall(SYS_MSI_INFO, (long)out, max, 0);
}

/* ---- Work Unit B wrappers ------------------------------------------------ */

long getppid(void)  { return __syscall(SYS_GETPPID, 0, 0, 0); }
long gettid(void)   { return __syscall(SYS_GETTID, 0, 0, 0); }
long getuid(void)   { return __syscall(SYS_GETUID, 0, 0, 0); }
long setuid(long uid) { return __syscall(SYS_SETUID, uid, 0, 0); }
long getgid(void)   { return __syscall(SYS_GETGID, 0, 0, 0); }
long setgid(long gid) { return __syscall(SYS_SETGID, gid, 0, 0); }

int  getpriority(int which, long who) {
    return (int)__syscall(SYS_GETPRIORITY, which, who, 0);
}
int  setpriority(int which, long who, int prio) {
    return (int)__syscall(SYS_SETPRIORITY, which, who, prio);
}

long brk(unsigned long addr) { return __syscall(SYS_BRK, (long)addr, 0, 0); }
void *sbrk(long delta) {
    long r = __syscall(SYS_SBRK, delta, 0, 0);
    if (r < 0) {
        return (void *)-1;
    }
    return (void *)r;
}
void *mmap(void *addr, unsigned long len, int prot, int flags) {
    long r = __syscall(SYS_MMAP, (long)addr, (long)len, prot);
    (void)flags;
    if (r < 0) {
        return (void *)-1;
    }
    return (void *)r;
}
int munmap(void *addr, unsigned long len) {
    return (int)__syscall(SYS_MUNMAP, (long)addr, (long)len, 0);
}

int dup(int fd)            { return (int)__syscall(SYS_DUP, fd, 0, 0); }
int dup2(int oldfd, int newfd) { return (int)__syscall(SYS_DUP2, oldfd, newfd, 0); }
int fcntl(int fd, int cmd, long arg) { return (int)__syscall(SYS_FCNTL, fd, cmd, arg); }
int ioctl(int fd, unsigned long req, long arg) { return (int)__syscall(SYS_IOCTL, fd, (long)req, arg); }
int pipe(int fds[2])       { return (int)__syscall(SYS_PIPE, (long)fds, 0, 0); }

long waitpid(long pid, long *status, int options) {
    return __syscall(SYS_WAITPID, pid, (long)status, options);
}
int kill(long pid, int sig) { return (int)__syscall(SYS_KILL, pid, sig, 0); }

int gettimeofday(struct sys_timeval *tv) {
    return (int)__syscall(SYS_GETTIMEOFDAY, (long)tv, 0, 0);
}
int clock_gettime(int clk, struct sys_timespec *ts) {
    return (int)__syscall(SYS_CLOCK_GETTIME, clk, (long)ts, 0);
}
int nanosleep(const struct sys_timespec *ts) {
    return (int)__syscall(SYS_NANOSLEEP, (long)ts, 0, 0);
}

long getpgid(long pid)            { return __syscall(SYS_GETPGID, pid, 0, 0); }
int  setpgid(long pid, long pgid) { return (int)__syscall(SYS_SETPGID, pid, pgid, 0); }
long getsid(long pid)             { return __syscall(SYS_GETSID, pid, 0, 0); }
long setsid(void)                 { return __syscall(SYS_SETSID, 0, 0, 0); }

int env_set(const char *kv)                  { return (int)__syscall(SYS_ENV_SET, (long)kv, 0, 0); }
int env_get(const char *key, char *out)      { return (int)__syscall(SYS_ENV_GET, (long)key, (long)out, 0); }
