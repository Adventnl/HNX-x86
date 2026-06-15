/* cut -f<list> [-d<delim>] [file] : select delimiter-separated fields.
 * cut -c<list> [file]             : select character positions.
 * <list> is a comma-separated set of 1-based field/char numbers (no ranges).
 * Default field delimiter is TAB. Reads stdin when given no file. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"

#define MAXSEL 64
static int  g_sel[MAXSEL];
static int  g_nsel;

static int wanted(int field) {
    for (int k = 0; k < g_nsel; k++) {
        if (g_sel[k] == field) {
            return 1;
        }
    }
    return 0;
}

static void parse_list(const char *s) {
    while (*s && g_nsel < MAXSEL) {
        if (isdigit((unsigned char)*s)) {
            int v = 0;
            while (isdigit((unsigned char)*s)) {
                v = v * 10 + (*s - '0');
                s++;
            }
            g_sel[g_nsel++] = v;
        }
        if (*s == ',') {
            s++;
        } else if (*s) {
            break;
        }
    }
}

int main(int argc, char **argv) {
    int char_mode = 0;
    char delim = '\t';
    const char *list = NULL;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char f = argv[i][1];
        const char *rest = argv[i] + 2;
        if (f == 'f') { list = rest; }
        else if (f == 'c') { char_mode = 1; list = rest; }
        else if (f == 'd') { delim = rest[0] ? rest[0] : '\t'; }
        else { eprint("cut: unknown flag\n"); return 2; }
    }
    if (!list || !*list) {
        eprint("usage: cut -f<list>|-c<list> [-d<delim>] [file]\n");
        return 2;
    }
    parse_list(list);

    int fd = 0;
    if (i < argc) {
        fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cut: %s: cannot open\n", argv[i]);
            return 1;
        }
    }
    char line[1024];
    long n;
    while ((n = fdgets(fd, line, sizeof(line))) > 0) {
        size_t len = strlen(line);
        int has_nl = (len && line[len - 1] == '\n');
        if (has_nl) {
            line[--len] = 0;
        }
        if (char_mode) {
            for (size_t k = 0; k < len; k++) {
                if (wanted((int)k + 1)) {
                    write(1, &line[k], 1);
                }
            }
        } else {
            int field = 1;
            int first_out = 1;
            const char *start = line;
            for (size_t k = 0; k <= len; k++) {
                if (line[k] == delim || line[k] == 0) {
                    if (wanted(field)) {
                        if (!first_out) write(1, &delim, 1);
                        write(1, start, (unsigned long)(&line[k] - start));
                        first_out = 0;
                    }
                    field++;
                    start = &line[k + 1];
                    if (line[k] == 0) break;
                }
            }
        }
        write(1, "\n", 1);
    }
    if (fd != 0) {
        close(fd);
    }
    return 0;
}
