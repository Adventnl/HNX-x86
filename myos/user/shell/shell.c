/* MyOS shell v0.
 *
 * No keyboard yet: the shell drains its scripted command stream from stdin
 * (/dev/console, pre-loaded by the kernel), then executes each line. Builtins
 * (cd/exit) run in-process; everything else is resolved to /bin/<cmd>.hxe and
 * spawned. Prints "[PASS] shell scripted session" when the script ends. */
#include "stdio.h"
#include "unistd.h"
#include "string.h"
#include "parser.h"
#include "builtins.h"

static char g_input[4096];

static void resolve_path(const char *cmd, char *out, unsigned long out_size) {
    if (strchr(cmd, '/')) {
        unsigned long i = 0;
        for (; cmd[i] && i < out_size - 1; i++) {
            out[i] = cmd[i];
        }
        out[i] = 0;
    } else {
        out[0] = 0;
        strcat(out, "/bin/");
        strcat(out, cmd);
        strcat(out, ".hxe");
    }
}

static void run_line(char *line, int *want_exit) {
    char *argv[SHELL_MAX_ARGS];
    int argc = parse_line(line, argv, SHELL_MAX_ARGS);
    if (argc == 0) {
        return;
    }
    if (builtin_try(argc, argv, want_exit)) {
        return;
    }
    char path[256];
    resolve_path(argv[0], path, sizeof(path));
    long pid = spawn(path, argv);
    if (pid < 0) {
        printf("shell: command not found: %s\n", argv[0]);
        return;
    }
    long code = 0;
    wait_pid(pid, &code);
}

int main(void) {
    print("[shell] scripted session start\n");

    /* Drain the entire scripted input up front (so spawned children never race
     * for the shared console). */
    long total = 0, n;
    while (total < (long)sizeof(g_input) - 1 &&
           (n = read(0, g_input + total, sizeof(g_input) - 1 - total)) > 0) {
        total += n;
    }
    g_input[total] = 0;

    int want_exit = 0;
    char *p = g_input;
    while (*p && !want_exit) {
        char *line = p;
        char *nl = strchr(p, '\n');
        if (nl) {
            *nl = 0;
            p = nl + 1;
        } else {
            p += strlen(p);
        }
        if (line[0] == 0) {
            continue;
        }
        printf("myos$ %s\n", line);
        run_line(line, &want_exit);
    }

    print("[PASS] shell scripted session\n");
    return 0;
}
