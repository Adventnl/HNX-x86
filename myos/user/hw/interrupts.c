/* interrupts: per-vector interrupt counts from SYS_INTERRUPTS. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_irq_entry e[32];
    int n = interrupts(e, 32);
    if (n < 0) {
        print("interrupts: error\n");
        return 1;
    }
    printf("%-8s %s\n", "VECTOR", "COUNT");
    for (int i = 0; i < n; i++) {
        printf("0x%x     %u\n", e[i].vector, (unsigned)e[i].count);
    }
    if (n == 0) {
        print("(no active interrupt vectors)\n");
    }
    return 0;
}
