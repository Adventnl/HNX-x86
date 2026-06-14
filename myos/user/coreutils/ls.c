/* ls: list a directory (default ".") via the readdir syscall. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

static const char *type_tag(unsigned int type) {
    switch (type) {
    case 1:  return "/";   /* directory */
    case 2:  return "@";   /* char device */
    default: return "";
    }
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";
    int fd = open(path, O_DIRECTORY);
    if (fd < 0) {
        printf("ls: %s: not a directory\n", path);
        return 1;
    }
    struct sys_dirent de;
    int count = 0;
    while (readdir(fd, &de) == 1 && count < 256) {
        printf("%s%s\n", de.name, type_tag(de.type));
        count++;
    }
    close(fd);
    return 0;
}
