/* MyOS shell.
 *
 *   shell      scripted mode  : echoes + runs lines until "exit"/EOF,
 *                               prints "[PASS] shell scripted session".
 *   shell -i   interactive    : prints a "myos:<cwd>$ " prompt, reads a cooked
 *                               line (canonical TTY), runs it; prints
 *                               "[PASS] shell interactive smoke".
 *
 * Both read one line at a time from stdin (/dev/console), so the scripted shell
 * stops at its "exit" and leaves any following lines for the interactive shell.
 * Builtins (cd/exit) run in-process; other commands resolve to /bin/<cmd>.hxe. */
#include "stdio.h"
#include "unistd.h"
#include "string.h"
#include "parser.h"
#include "builtins.h"

static int read_line(char *buf, int max) {
    int n = 0;
    char c;
    while (n < max - 1) {
        long r = read(0, &c, 1);
        if (r <= 0) {
            if (n == 0) {
                return -1;        /* EOF */
            }
            break;
        }
        if (c == '\n') {
            break;
        }
        buf[n++] = c;
    }
    buf[n] = 0;
    return n;
}

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

int main(int argc, char **argv) {
    int interactive = (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'i');
    if (!interactive) {
        print("[shell] scripted session start\n");
    } else {
        print("[shell] interactive session start\n");
    }

    char line[256];
    char cwd[128];
    int want_exit = 0;
    while (!want_exit) {
        if (interactive) {
            if (getcwd(cwd, sizeof(cwd)) < 0) {
                strcpy(cwd, "/");
            }
            printf("myos:%s$ ", cwd);
        }
        int n = read_line(line, sizeof(line));
        if (n < 0) {
            break;                /* EOF */
        }
        if (!interactive) {
            printf("myos$ %s\n", line);
        }
        if (n == 0) {
            continue;
        }
        run_line(line, &want_exit);
    }

    print(interactive ? "[PASS] shell interactive smoke\n"
                      : "[PASS] shell scripted session\n");
    return 0;
}
