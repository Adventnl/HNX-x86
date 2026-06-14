/* clear: emit the ANSI clear-screen + cursor-home sequence. */
#include "stdio.h"

int main(void) {
    print("\033[2J\033[H");
    return 0;
}
