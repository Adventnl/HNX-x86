/* help: list the available shell commands / coreutils. */
#include "stdio.h"

int main(void) {
    print("MyOS shell — available commands:\n");
    print("  builtins : cd <dir>, exit\n");
    print("  coreutils: echo cat ls pwd clear help true false yes\n");
    print("             whoami uptime meminfo ps testread hello\n");
    print("  usage    : <command> [args]   (resolved as /bin/<command>.hxe)\n");
    return 0;
}
