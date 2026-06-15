/* sleep <seconds>: suspend for the given number of whole seconds. */
#include "stdio.h"
#include "unistd.h"
#include "stdlib.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        eprint("usage: sleep <seconds>\n");
        return 2;
    }
    long secs = atol(argv[1]);
    if (secs < 0) {
        secs = 0;
    }
    sleep_ms((unsigned long)secs * 1000UL);
    return 0;
}
