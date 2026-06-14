/* echo: print arguments separated by spaces, followed by a newline. */
#include "stdio.h"
#include "unistd.h"

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            write(1, " ", 1);
        }
        print(argv[i]);
    }
    write(1, "\n", 1);
    return 0;
}
