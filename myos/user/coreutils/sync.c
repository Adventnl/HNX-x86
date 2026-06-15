/* sync: flush filesystem buffers to stable storage.
 *
 * MyOS has no sync syscall: the VFS is write-through / in-memory, so there are
 * no dirty buffers to flush. This command therefore succeeds immediately,
 * matching the POSIX contract that sync() always returns. */
#include "stdio.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 0;
}
