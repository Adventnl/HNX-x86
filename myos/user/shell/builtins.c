/* Shell builtins + variable/history state. */
#include "builtins.h"
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"

/* ---- variable table ------------------------------------------------------ */
static char g_var_name[SHELL_MAX_VARS][SHELL_VAR_NAME];
static char g_var_value[SHELL_MAX_VARS][SHELL_VAR_VALUE];
static int  g_var_count;

static int var_find(const char *name) {
    for (int i = 0; i < g_var_count; i++) {
        if (strcmp(g_var_name[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

void shell_var_set(const char *name, const char *value) {
    int i = var_find(name);
    if (i < 0) {
        if (g_var_count >= SHELL_MAX_VARS) {
            return;
        }
        i = g_var_count++;
        strncpy(g_var_name[i], name, SHELL_VAR_NAME - 1);
        g_var_name[i][SHELL_VAR_NAME - 1] = 0;
    }
    strncpy(g_var_value[i], value, SHELL_VAR_VALUE - 1);
    g_var_value[i][SHELL_VAR_VALUE - 1] = 0;
}

const char *shell_var_get(const char *name) {
    int i = var_find(name);
    return i < 0 ? (const char *)0 : g_var_value[i];
}

void shell_var_unset(const char *name) {
    int i = var_find(name);
    if (i < 0) {
        return;
    }
    g_var_count--;
    if (i != g_var_count) {
        strcpy(g_var_name[i], g_var_name[g_var_count]);
        strcpy(g_var_value[i], g_var_value[g_var_count]);
    }
}

/* ---- history ring -------------------------------------------------------- */
static char g_hist[SHELL_HISTORY][SHELL_HIST_LEN];
static int  g_hist_count;     /* total recorded */

void shell_history_record(const char *line) {
    if (!line || !line[0]) {
        return;
    }
    int slot = g_hist_count % SHELL_HISTORY;
    strncpy(g_hist[slot], line, SHELL_HIST_LEN - 1);
    g_hist[slot][SHELL_HIST_LEN - 1] = 0;
    g_hist_count++;
}

int shell_history_count(void) {
    return g_hist_count;
}

static void history_print(void) {
    int total = g_hist_count;
    int start = total > SHELL_HISTORY ? total - SHELL_HISTORY : 0;
    for (int i = start; i < total; i++) {
        printf("%4d  %s\n", i + 1, g_hist[i % SHELL_HISTORY]);
    }
}

/* ---- which: search /bin/<cmd>.hxe ---------------------------------------- */
static int which_path(const char *cmd, char *out, int max) {
    if (strchr(cmd, '/')) {
        strncpy(out, cmd, max - 1);
        out[max - 1] = 0;
    } else {
        out[0] = 0;
        strcat(out, "/bin/");
        strcat(out, cmd);
        strcat(out, ".hxe");
    }
    int fd = open(out, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    return 0;
}

/* ---- the expanded-shell self test (drives the new machinery) ------------- */
static void run_selftest(void) {
    int ok = 1;

    shell_var_set("FOO", "bar");
    ok &= (shell_var_get("FOO") && strcmp(shell_var_get("FOO"), "bar") == 0);
    shell_var_set("FOO", "baz");                 /* overwrite */
    ok &= (shell_var_get("FOO") && strcmp(shell_var_get("FOO"), "baz") == 0);
    shell_var_unset("FOO");
    ok &= (shell_var_get("FOO") == (const char *)0);

    int before = shell_history_count();
    shell_history_record("echo hi");
    ok &= (shell_history_count() == before + 1);

    char path[128];
    ok &= (which_path("echo", path, sizeof(path)) == 0);   /* /bin/echo.hxe exists */
    ok &= (which_path("definitely_not_a_cmd", path, sizeof(path)) != 0);

    print(ok ? "[PASS] shell expanded tests\n" : "[FAIL] shell expanded tests\n");
}

/* ---- builtin dispatch ---------------------------------------------------- */
int builtin_try(int argc, char *argv[], int *want_exit) {
    const char *cmd = argv[0];

    if (strcmp(cmd, "exit") == 0) {
        *want_exit = 1;
        return 1;
    }
    if (strcmp(cmd, "cd") == 0) {
        const char *target = (argc > 1) ? argv[1] : "/";
        if (chdir(target) < 0) {
            printf("cd: %s: no such directory\n", target);
        }
        return 1;
    }
    if (strcmp(cmd, "pwd") == 0) {
        char cwd[128];
        if (getcwd(cwd, sizeof(cwd)) >= 0) {
            printf("%s\n", cwd);
        }
        return 1;
    }
    if (strcmp(cmd, "set") == 0) {
        if (argc >= 3) {
            shell_var_set(argv[1], argv[2]);
        } else if (argc == 2) {
            const char *v = shell_var_get(argv[1]);
            printf("%s=%s\n", argv[1], v ? v : "");
        }
        return 1;
    }
    if (strcmp(cmd, "unset") == 0) {
        if (argc >= 2) {
            shell_var_unset(argv[1]);
        }
        return 1;
    }
    if (strcmp(cmd, "export") == 0) {
        /* export NAME=VALUE  -> also push to the process environment */
        if (argc >= 2) {
            char *eq = strchr(argv[1], '=');
            if (eq) {
                *eq = 0;
                shell_var_set(argv[1], eq + 1);
                *eq = '=';
                env_set(argv[1]);
            }
        }
        return 1;
    }
    if (strcmp(cmd, "history") == 0) {
        history_print();
        return 1;
    }
    if (strcmp(cmd, "which") == 0) {
        if (argc >= 2) {
            char path[128];
            if (which_path(argv[1], path, sizeof(path)) == 0) {
                printf("%s\n", path);
            } else {
                printf("%s: not found\n", argv[1]);
            }
        }
        return 1;
    }
    if (strcmp(cmd, "help") == 0) {
        print("builtins: cd pwd set unset export history which help "
              "selftest exit\n");
        return 1;
    }
    if (strcmp(cmd, "selftest") == 0) {
        run_selftest();
        return 1;
    }
    return 0;
}
