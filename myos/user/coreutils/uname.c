/* uname [-a]: print system identification. */
#include "stdio.h"
#include "unistd.h"
#include "string.h"

int main(int argc, char **argv) {
    int all = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            all = 1;
        }
    }
    if (all) {
        printf("HNX MyOS x86_64 HNX MyOS\n");
    } else {
        printf("HNX MyOS x86_64\n");
    }
    return 0;
}
