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
