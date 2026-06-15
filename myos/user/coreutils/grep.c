/* grep [-n] [-i] [-v] <pattern> [files...]: print lines containing pattern
 * (plain substring match). Reads stdin when given no files. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"
#include "ctype.h"

static int g_icase, g_invert, g_number;

static int contains(const char *line, const char *pat) {
    if (!g_icase) {
        return strstr(line, pat) != NULL;
    }
    /* Case-insensitive substring search. */
    if (!*pat) {
        return 1;
    }
    for (const char *h = line; *h; h++) {
        const char *a = h, *b = pat;
        while (*a && *b &&
               tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b) {
            return 1;
        }
    }
    return 0;
}

static int grep_fd(int fd, const char *pat, const char *name, int show_name) {
    char line[1024];
    long n;
    unsigned long lineno = 0;
    int matched = 0;
    while ((n = fdgets(fd, line, sizeof(line))) > 0) {
        lineno++;
        /* Strip trailing newline for matching/printing control. */
        int hit = contains(line, pat);
        if (hit == !g_invert) {
            matched = 1;
            if (show_name) printf("%s:", name);
            if (g_number) printf("%lu:", lineno);
            print(line);
            if (line[0] == 0 || line[strlen(line) - 1] != '\n') {
                write(1, "\n", 1);
            }
        }
    }
    return matched;
}

int main(int argc, char **argv) {
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'i') g_icase = 1;
            else if (*p == 'v') g_invert = 1;
            else if (*p == 'n') g_number = 1;
            else { eprint("grep: unknown flag\n"); return 2; }
        }
    }
    if (i >= argc) {
        eprint("usage: grep [-niv] <pattern> [files...]\n");
        return 2;
    }
    const char *pat = argv[i++];
    int any_match = 0;
    if (i >= argc) {
        any_match = grep_fd(0, pat, "(stdin)", 0);
        return any_match ? 0 : 1;
    }
    int show_name = (argc - i) > 1;
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("grep: %s: cannot open\n", argv[i]);
            continue;
        }
        if (grep_fd(fd, pat, argv[i], show_name)) {
            any_match = 1;
        }
        close(fd);
    }
    return any_match ? 0 : 1;
}
