/* Per-process file-descriptor table: a small fixed array of open-file pointers.
 * fd 0/1/2 are wired to /dev/console at process creation. */
#ifndef MYOS_PROCESS_FD_TABLE_H
#define MYOS_PROCESS_FD_TABLE_H

#include "types.h"

#define FD_MAX 32

struct file;

struct fd_table {
    struct file *entries[FD_MAX];
};

struct fd_table *fd_table_create(void);
void             fd_table_destroy(struct fd_table *t);   /* unref all + free */

/* Install `f` (caller-owned ref) in the lowest free slot; returns fd or -EMFILE. */
int          fd_alloc(struct fd_table *t, struct file *f);
/* Install `f` at a specific fd, replacing any current occupant; returns fd. */
int          fd_install_at(struct fd_table *t, int fd, struct file *f);
int          fd_close(struct fd_table *t, int fd);        /* 0 or -EBADF */
struct file *fd_get(struct fd_table *t, int fd);          /* NULL if unused */

#endif /* MYOS_PROCESS_FD_TABLE_H */
