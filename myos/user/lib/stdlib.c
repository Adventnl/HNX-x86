/* exit + numeric parsing + qsort/bsearch + strerror.
 * (malloc/free/calloc/realloc live in malloc.c.) */
#include "stdlib.h"
#include "string.h"
#include "errno.h"
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

long atol(const char *s) {
    return strtol(s, (char **)0, 10);
}

static int digit_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

long strtol(const char *s, char **endptr, int base) {
    const char *start = s;
    long sign = 1, v = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' ||
           *s == '\r' || *s == '\v' || *s == '\f') {
        s++;
    }
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    if ((base == 0 || base == 16) &&
        s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && digit_val(s[2]) >= 0 &&
        digit_val(s[2]) < 16) {
        s += 2;
        base = 16;
    } else if (base == 0 && s[0] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }
    int any = 0;
    for (;;) {
        int d = digit_val(*s);
        if (d < 0 || d >= base) {
            break;
        }
        v = v * base + d;
        any = 1;
        s++;
    }
    if (endptr) {
        *endptr = (char *)(any ? s : start);
    }
    return sign * v;
}

int  abs(int v)  { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }

/* Simple insertion sort: stable, no recursion, fine for the small arrays a
 * freestanding userland sorts. Uses a stack scratch buffer for the element. */
void qsort(void *base, size_t nmemb, size_t size,
           int (*cmp)(const void *, const void *)) {
    unsigned char *a = (unsigned char *)base;
    unsigned char tmp[256];
    if (size == 0 || size > sizeof(tmp)) {
        return;
    }
    for (size_t i = 1; i < nmemb; i++) {
        memcpy(tmp, a + i * size, size);
        size_t j = i;
        while (j > 0 && cmp(a + (j - 1) * size, tmp) > 0) {
            memcpy(a + j * size, a + (j - 1) * size, size);
            j--;
        }
        memcpy(a + j * size, tmp, size);
    }
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *)) {
    const unsigned char *a = (const unsigned char *)base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int r = cmp(key, a + mid * size);
        if (r == 0) {
            return (void *)(a + mid * size);
        } else if (r < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return (void *)0;
}

const char *strerror(int err) {
    if (err < 0) {
        err = -err;
    }
    switch (err) {
    case 0:                return "Success";
    case SYS_EPERM:        return "Operation not permitted";
    case SYS_ENOENT:       return "No such file or directory";
    case SYS_ESRCH:        return "No such process";
    case SYS_EIO:          return "I/O error";
    case SYS_ENOEXEC:      return "Exec format error";
    case SYS_EBADF:        return "Bad file descriptor";
    case SYS_ECHILD:       return "No child processes";
    case SYS_ENOMEM:       return "Out of memory";
    case SYS_EFAULT:       return "Bad address";
    case SYS_EEXIST:       return "File exists";
    case SYS_ENOTDIR:      return "Not a directory";
    case SYS_EISDIR:       return "Is a directory";
    case SYS_EINVAL:       return "Invalid argument";
    case SYS_EMFILE:       return "Too many open files";
    case SYS_ERANGE:       return "Result too large";
    case SYS_ENAMETOOLONG: return "File name too long";
    case SYS_ENOSYS:       return "Function not implemented";
    default:               return "Unknown error";
    }
}
