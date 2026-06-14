/* stat <path>: print a file's type and size. */
#include "stdio.h"
#include "unistd.h"

static const char *type_name(unsigned int t) {
    switch (t) {
    case 0:  return "file";
    case 1:  return "dir";
    case 2:  return "chardev";
    default: return "?";
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print("usage: stat <path>\n");
        return 1;
    }
    struct sys_stat st;
    if (stat(argv[1], &st) < 0) {
        printf("stat: cannot stat '%s'\n", argv[1]);
        return 1;
    }
    printf("%s: type=%s size=%lu\n", argv[1], type_name(st.type),
           (unsigned long)st.size);
    return 0;
}
