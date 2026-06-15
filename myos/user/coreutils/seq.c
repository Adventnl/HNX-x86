/* seq [first [incr]] last : print an arithmetic sequence, one number per line.
 *   seq L        -> 1..L step 1
 *   seq F L      -> F..L step 1
 *   seq F I L    -> F..L step I */
#include "stdio.h"
#include "unistd.h"
#include "stdlib.h"

int main(int argc, char **argv) {
    long first = 1, incr = 1, last = 0;
    if (argc == 2) {
        last = atol(argv[1]);
    } else if (argc == 3) {
        first = atol(argv[1]);
        last  = atol(argv[2]);
    } else if (argc == 4) {
        first = atol(argv[1]);
        incr  = atol(argv[2]);
        last  = atol(argv[3]);
    } else {
        eprint("usage: seq [first [incr]] last\n");
        return 2;
    }
    if (incr == 0) {
        eprint("seq: increment must be nonzero\n");
        return 2;
    }
    if (incr > 0) {
        for (long v = first; v <= last; v += incr) {
            printf("%ld\n", v);
        }
    } else {
        for (long v = first; v >= last; v += incr) {
            printf("%ld\n", v);
        }
    }
    return 0;
}
