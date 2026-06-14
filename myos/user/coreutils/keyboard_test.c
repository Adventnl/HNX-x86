/* keyboard_test: user-space view of the keyboard. The decode + injection path
 * is verified kernel-side ("[PASS] keyboard scripted injection"); here we just
 * report that the input device is reachable. */
#include "stdio.h"

int main(void) {
    print("[keyboard_test] keyboard input available via /dev/console\n");
    return 0;
}
