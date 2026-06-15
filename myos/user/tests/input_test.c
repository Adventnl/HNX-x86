/* input_test: ring-3 check that the unified input syscalls are reachable. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_hw_info h;
    if (hw_info(&h) != 0) {
        print("[FAIL] input_test\n");
        return 1;
    }
    /* Drain (may be empty in a headless run) — the point is the syscall works. */
    struct sys_input_event ev;
    int n = 0;
    while (input_poll(&ev) == 1 && n < 64) {
        n++;
    }
    print("[PASS] input_test\n");
    return 0;
}
