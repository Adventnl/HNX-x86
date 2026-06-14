/* Shell builtins that must run in the shell's own process: cd and exit. */
#ifndef MYOS_SHELL_BUILTINS_H
#define MYOS_SHELL_BUILTINS_H

/* If argv[0] is a builtin, handle it and return 1 (setting *want_exit for exit);
 * otherwise return 0. */
int builtin_try(int argc, char *argv[], int *want_exit);

#endif /* MYOS_SHELL_BUILTINS_H */
