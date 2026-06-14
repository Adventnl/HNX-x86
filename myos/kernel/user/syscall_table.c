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
};

syscall_fn syscall_table_get(uint64_t nr) {
    if (nr >= SYS_MAX_NR) {
        return (syscall_fn)0;
    }
    return g_table[nr];
}
