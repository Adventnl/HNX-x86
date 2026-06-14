/* fault_test: deliberately dereference an unmapped user address to prove ring-3
 * fault isolation. The kernel terminates this process and prints
 * "[OK] User fault isolated" (greped by verify-user-fault); the system survives.
 * This program never returns normally. */
#include "stdio.h"

int main(void) {
    print("[test] fault_test: triggering a ring-3 page fault\n");
    volatile int *bad = (volatile int *)0x1;   /* page 0 is unmapped */
    *bad = 0xdead;
    print("[test] fault_test: ERROR — fault not taken\n");
    return 0;
}
