/* wc [-l|-w|-c] [files...]: count lines, words, and bytes. With no flags prints
 * all three. Reads stdin when given no files. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "ctype.h"
#include "string.h"

struct counts { unsigned long lines, words, bytes; };

static void count_fd(int fd, struct counts *c) {
    char buf[512];
    long n;
    int in_word = 0;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < n; i++) {
            char ch = buf[i];
            c->bytes++;
            if (ch == '\n') {
                c->lines++;
            }
            if (isspace((unsigned char)ch)) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                c->words++;
            }
        }
    }
}

static void report(struct counts *c, int sl, int sw, int sc, const char *name) {
    int any = sl || sw || sc;
    if (!any) {
        sl = sw = sc = 1;
    }
    if (sl) printf("%8lu", c->lines);
    if (sw) printf("%8lu", c->words);
    if (sc) printf("%8lu", c->bytes);
    if (name) printf(" %s", name);
    printf("\n");
}

int main(int argc, char **argv) {
    int sl = 0, sw = 0, sc = 0;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'l') sl = 1;
            else if (*p == 'w') sw = 1;
            else if (*p == 'c') sc = 1;
            else { eprint("wc: unknown flag\n"); return 2; }
        }
    }
    if (i >= argc) {
        struct counts c = {0, 0, 0};
        count_fd(0, &c);
        report(&c, sl, sw, sc, NULL);
        return 0;
    }
    struct counts total = {0, 0, 0};
    int rc = 0, nfiles = 0;
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("wc: %s: cannot open\n", argv[i]);
            rc = 1;
            continue;
        }
        struct counts c = {0, 0, 0};
        count_fd(fd, &c);
        close(fd);
        report(&c, sl, sw, sc, argv[i]);
        total.lines += c.lines;
        total.words += c.words;
        total.bytes += c.bytes;
        nfiles++;
    }
    if (nfiles > 1) {
        report(&total, sl, sw, sc, "total");
    }
    return rc;
}
