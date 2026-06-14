/* Whitespace tokenizer with minimal double-quote support. */
#include "parser.h"

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

int parse_line(char *line, char *argv[], int max) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max - 1) {
        while (is_space(*p)) {
            p++;
        }
        if (*p == 0) {
            break;
        }
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') {
                p++;
            }
            if (*p == '"') {
                *p++ = 0;
            }
        } else {
            argv[argc++] = p;
            while (*p && !is_space(*p)) {
                p++;
            }
            if (*p) {
                *p++ = 0;
            }
        }
    }
    argv[argc] = (char *)0;
    return argc;
}
