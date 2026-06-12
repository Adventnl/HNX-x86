/* User convenience helpers layered on the syscalls. */
#include "stdlib.h"
#include "string.h"
#include "syscall.h"

long print(const char *s) {
    return sys_write(1, s, strlen(s));
}

long eprint(const char *s) {
    return sys_write(2, s, strlen(s));
}

void print_u64(uint64_t value) {
    char buf[21];
    int i = 20;
    buf[i] = 0;
    if (value == 0) {
        sys_write(1, "0", 1);
        return;
    }
    while (value && i > 0) {
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
    }
    sys_write(1, &buf[i], strlen(&buf[i]));
}

void print_i64(int64_t value) {
    if (value < 0) {
        sys_write(1, "-", 1);
        print_u64((uint64_t)(-value));
    } else {
        print_u64((uint64_t)value);
    }
}
