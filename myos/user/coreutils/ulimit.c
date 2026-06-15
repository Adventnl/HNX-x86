/* ulimit: report the per-process resource limits (foundation: fixed values). */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    /* These mirror the kernel's compile-time limits. */
    printf("open files (-n)        %d\n", 32);          /* FD_MAX */
    printf("max args   (-A)        %d\n", 32);          /* PROCESS_MAX_ARGS */
    printf("stack size (-s)        %d KiB\n", 256);     /* USER_STACK_SIZE */
    printf("brk arena  (-d)        %d KiB\n", 256);     /* PROCESS_BRK_INITIAL */
    printf("processes  (-u)        %d\n", 64);          /* PROCESS_MAX */
    return 0;
}
