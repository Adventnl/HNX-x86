/* yes: print a string repeatedly. Bounded here (the scripted environment has no
 * way to interrupt an infinite loop yet); defaults to "y" eight times. */
#include "stdio.h"

int main(int argc, char **argv) {
    const char *s = (argc > 1) ? argv[1] : "y";
    for (int i = 0; i < 8; i++) {
        printf("%s\n", s);
    }
    return 0;
}
