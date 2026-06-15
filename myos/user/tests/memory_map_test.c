/* Work Unit B userland test: program break (brk/sbrk) and the anonymous mmap
 * foundation. Marker: "[PASS] user memory map tests". */
#include "stdio.h"
#include "unistd.h"
#include "syscall.h"   /* PROT_* / MAP_* */

int main(void) {
    int ok = 1;

    /* sbrk(0) reports the current break. */
    void *base = sbrk(0);
    ok &= (base != (void *)-1);

    /* Grow by 4 KiB and use the new memory. */
    void *p = sbrk(4096);
    ok &= (p != (void *)-1);
    if (p != (void *)-1) {
        volatile unsigned char *m = (volatile unsigned char *)p;
        for (int i = 0; i < 4096; i++) {
            m[i] = (unsigned char)(i & 0xFF);
        }
        int good = 1;
        for (int i = 0; i < 4096; i++) {
            if (m[i] != (unsigned char)(i & 0xFF)) {
                good = 0;
            }
        }
        ok &= good;
    }

    /* The break advanced by exactly one page. */
    void *after = sbrk(0);
    ok &= ((unsigned long)after - (unsigned long)base == 4096);

    /* Shrink back down. */
    sbrk(-4096);
    void *shrunk = sbrk(0);
    ok &= (shrunk == base);

    /* mmap an anonymous region, write it, then unmap. */
    void *r = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS);
    ok &= (r != (void *)-1);
    if (r != (void *)-1) {
        volatile unsigned long *q = (volatile unsigned long *)r;
        q[0] = 0xCAFEBABEUL;
        q[500] = 0xDEADBEEFUL;
        ok &= (q[0] == 0xCAFEBABEUL && q[500] == 0xDEADBEEFUL);
        ok &= (munmap(r, 8192) == 0);
    }

    print(ok ? "[PASS] user memory map tests\n" : "[FAIL] user memory map tests\n");
    return ok ? 0 : 1;
}
