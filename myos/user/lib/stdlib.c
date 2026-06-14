/* exit + numeric parsing. (malloc/free/calloc live in malloc.c.) */
#include "stdlib.h"
#include "syscall.h"

void exit(int code) {
    __syscall(SYS_EXIT, code, 0, 0);
    for (;;) {           /* exit never returns */
    }
}

int atoi(const char *s) {
    int sign = 1, v = 0;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return sign * v;
}
