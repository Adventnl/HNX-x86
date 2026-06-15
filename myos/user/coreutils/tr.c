/* tr <set1> <set2>: translate bytes of set1 to the matching byte of set2, or
 * with -d delete bytes in set1. Reads stdin, writes stdout. No ranges/classes. */
#include "stdio.h"
#include "unistd.h"
#include "string.h"

int main(int argc, char **argv) {
    int del = 0;
    int i = 1;
    if (i < argc && argv[i][0] == '-' && argv[i][1] == 'd' && !argv[i][2]) {
        del = 1;
        i++;
    }
    if (del) {
        if (i + 1 != argc) {
            eprint("usage: tr -d <set>\n");
            return 2;
        }
    } else if (i + 2 != argc) {
        eprint("usage: tr <set1> <set2>\n");
        return 2;
    }
    const char *set1 = argv[i];
    const char *set2 = del ? "" : argv[i + 1];
    size_t l2 = strlen(set2);

    char buf[512];
    long n;
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        char out[512];
        long olen = 0;
        for (long k = 0; k < n; k++) {
            char c = buf[k];
            const char *hit = strchr(set1, c);
            if (hit && c != 0) {
                if (del) {
                    continue;        /* drop it */
                }
                size_t pos = (size_t)(hit - set1);
                /* Map to set2; if set2 is shorter, use its last char. */
                if (l2 == 0) {
                    out[olen++] = c;
                } else if (pos < l2) {
                    out[olen++] = set2[pos];
                } else {
                    out[olen++] = set2[l2 - 1];
                }
            } else {
                out[olen++] = c;
            }
        }
        if (olen) {
            write(1, out, (unsigned long)olen);
        }
    }
    return (n < 0) ? 1 : 0;
}
