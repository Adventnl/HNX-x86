/* The Prompt 4 syscall table (designated-initializer array indexed by number). */
#include "syscall_table.h"
#include "syscall_numbers.h"

static const syscall_fn g_table[SYS_MAX_NR] = {
    [SYS_EXIT]    = sys_exit,
    [SYS_WRITE]   = sys_write,
    [SYS_READ]    = sys_read,
    [SYS_SLEEP]   = sys_sleep,
    [SYS_GETPID]  = sys_getpid,
    [SYS_YIELD]   = sys_yield,
    [SYS_OPEN]    = sys_open,
    [SYS_CLOSE]   = sys_close,
    [SYS_LSEEK]   = sys_lseek,
    [SYS_READDIR] = sys_readdir,
    [SYS_SPAWN]   = sys_spawn,
    [SYS_WAIT]    = sys_wait,
    [SYS_GETCWD]  = sys_getcwd,
    [SYS_CHDIR]   = sys_chdir,
    [SYS_UPTIME]  = sys_uptime,
    [SYS_MEMINFO] = sys_meminfo,
    [SYS_PS]      = sys_ps,
    [SYS_MKDIR]   = sys_mkdir,
    [SYS_UNLINK]  = sys_unlink,
    [SYS_STAT]    = sys_stat,
    [SYS_MOUNT_INFO] = sys_mount_info,
    [SYS_DEVICES] = sys_devices,
    [SYS_BLOCKS]  = sys_blocks,
    [SYS_USB_DEVICES] = sys_usb_devices,
    [SYS_HW_INFO]     = sys_hw_info,
    [SYS_INTERRUPTS]  = sys_interrupts,
    [SYS_INPUT_POLL]  = sys_input_poll,
    [SYS_MOUSE_POLL]  = sys_mouse_poll,
    [SYS_MSI_INFO]    = sys_msi_info,
    /* Work Unit B. */
    [SYS_GETPPID]       = sys_getppid,
    [SYS_GETTID]        = sys_gettid,
    [SYS_GETUID]        = sys_getuid,
    [SYS_SETUID]        = sys_setuid,
    [SYS_GETGID]        = sys_getgid,
    [SYS_SETGID]        = sys_setgid,
    [SYS_GETPRIORITY]   = sys_getpriority,
    [SYS_SETPRIORITY]   = sys_setpriority,
    [SYS_BRK]           = sys_brk,
    [SYS_SBRK]          = sys_sbrk,
    [SYS_MMAP]          = sys_mmap,
    [SYS_MUNMAP]        = sys_munmap,
    [SYS_DUP]           = sys_dup,
    [SYS_DUP2]          = sys_dup2,
    [SYS_PIPE]          = sys_pipe,
    [SYS_FCNTL]         = sys_fcntl,
    [SYS_IOCTL]         = sys_ioctl,
    [SYS_WAITPID]       = sys_waitpid,
    [SYS_KILL]          = sys_kill,
    [SYS_GETTIMEOFDAY]  = sys_gettimeofday,
    [SYS_CLOCK_GETTIME] = sys_clock_gettime,
    [SYS_NANOSLEEP]     = sys_nanosleep,
    [SYS_GETPGID]       = sys_getpgid,
    [SYS_SETPGID]       = sys_setpgid,
    [SYS_GETSID]        = sys_getsid,
    [SYS_SETSID]        = sys_setsid,
    [SYS_ENV_GET]       = sys_env_get,
    [SYS_ENV_SET]       = sys_env_set,
};

syscall_fn syscall_table_get(uint64_t nr) {
    if (nr >= SYS_MAX_NR) {
        return (syscall_fn)0;
    }
    return g_table[nr];
}
