/* writefile <path> <text...>: create/overwrite a file with the joined text. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        print("usage: writefile <path> <text...>\n");
        return 1;
    }
    int fd = open(argv[1], O_CREAT | O_WRONLY | O_TRUNC);
    if (fd < 0) {
        printf("writefile: cannot open '%s'\n", argv[1]);
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        if (i > 2) {
            write(fd, " ", 1);
        }
        write(fd, argv[i], strlen(argv[i]));   /* offset auto-advances */
    }
    close(fd);
    return 0;
}
