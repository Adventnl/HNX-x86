/* Command-line tokenizer for the MyOS shell. */
#ifndef MYOS_SHELL_PARSER_H
#define MYOS_SHELL_PARSER_H

#define SHELL_MAX_ARGS 16

/* Split `line` in place into argv (whitespace-separated, simple "double quotes").
 * Writes at most max-1 args and NUL-terminates argv[argc]. Returns argc. */
int parse_line(char *line, char *argv[], int max);

#endif /* MYOS_SHELL_PARSER_H */
