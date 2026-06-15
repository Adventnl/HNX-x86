/* Work Unit H: user-space test runner. A small in-process test registry that
 * runs named cases and prints a structured summary — the userland counterpart
 * to the kernel test registry. Marker: "[PASS] user test runner". */
#include "stdio.h"
#include "unistd.h"
#include "string.h"
#include "stdlib.h"

typedef int (*test_fn)(void);

struct case_t {
    const char *name;
    test_fn     fn;
};

static int t_math(void)   { return (2 + 2 == 4) && (abs(-3) == 3); }
static int t_string(void) { return strcmp("a", "a") == 0 && strlen("xyz") == 3; }
static int t_mem(void) {
    char a[8], b[8];
    memset(a, 0x5A, 8);
    memcpy(b, a, 8);
    return memcmp(a, b, 8) == 0;
}
static int t_syscall(void) { return getpid() > 0 && uptime_ms() >= 0; }
static int t_alloc(void) {
    void *p = malloc(128);
    if (!p) return 0;
    memset(p, 0, 128);
    free(p);
    return 1;
}

static struct case_t g_cases[] = {
    { "math",    t_math },
    { "string",  t_string },
    { "mem",     t_mem },
    { "syscall", t_syscall },
    { "alloc",   t_alloc },
};
#define NCASES (int)(sizeof(g_cases) / sizeof(g_cases[0]))

int main(void) {
    int passed = 0, failed = 0;
    print("user test runner: running suite\n");
    for (int i = 0; i < NCASES; i++) {
        int ok = g_cases[i].fn();
        printf("  %-10s %s\n", g_cases[i].name, ok ? "ok" : "FAIL");
        if (ok) passed++; else failed++;
    }
    printf("user test runner: %d passed, %d failed\n", passed, failed);
    if (failed == 0 && passed == NCASES) {
        print("[PASS] user test runner\n");
        return 0;
    }
    print("[FAIL] user test runner\n");
    return 1;
}
