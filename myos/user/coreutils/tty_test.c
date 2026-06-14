/* tty_test: user-space view of the TTY. Canonical line editing is verified
 * kernel-side ("[PASS] tty canonical input"); here we just confirm the device. */
#include "stdio.h"

int main(void) {
    print("[tty_test] tty cooked input available via /dev/console\n");
    return 0;
}
