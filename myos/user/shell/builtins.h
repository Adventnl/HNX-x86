/* Shell builtins + in-process shell state (variables, history).
 *
 * Builtins that must run in the shell's own process (they mutate cwd, the
 * variable table, the history ring or request exit) are handled here. The
 * variable table backs $VAR expansion done by the shell before spawning. */
#ifndef MYOS_SHELL_BUILTINS_H
#define MYOS_SHELL_BUILTINS_H

#define SHELL_MAX_VARS    24
#define SHELL_VAR_NAME    32
#define SHELL_VAR_VALUE   96
#define SHELL_HISTORY     32
#define SHELL_HIST_LEN    128

/* If argv[0] is a builtin, handle it and return 1 (setting *want_exit for
 * exit); otherwise return 0. */
int builtin_try(int argc, char *argv[], int *want_exit);

/* Variable table (used by the parser/expander and the set/get builtins). */
void        shell_var_set(const char *name, const char *value);
const char *shell_var_get(const char *name);
void        shell_var_unset(const char *name);

/* Command history ring. */
void        shell_history_record(const char *line);
int         shell_history_count(void);

#endif /* MYOS_SHELL_BUILTINS_H */
