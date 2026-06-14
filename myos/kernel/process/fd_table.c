/* File-descriptor table. */
#include "fd_table.h"
#include "file.h"
#include "heap.h"
#include "syscall_numbers.h"

struct fd_table *fd_table_create(void) {
    return (struct fd_table *)kcalloc(1, sizeof(struct fd_table));
}

void fd_table_destroy(struct fd_table *t) {
    if (!t) {
        return;
    }
    for (int i = 0; i < FD_MAX; i++) {
        if (t->entries[i]) {
            file_unref(t->entries[i]);
            t->entries[i] = NULL;
        }
    }
    kfree(t);
}

int fd_alloc(struct fd_table *t, struct file *f) {
    if (!t || !f) {
        return -SYS_EINVAL;
    }
    for (int i = 0; i < FD_MAX; i++) {
        if (!t->entries[i]) {
            t->entries[i] = f;
            return i;
        }
    }
    return -SYS_EMFILE;
}

int fd_install_at(struct fd_table *t, int fd, struct file *f) {
    if (!t || fd < 0 || fd >= FD_MAX) {
        return -SYS_EBADF;
    }
    if (t->entries[fd]) {
        file_unref(t->entries[fd]);
    }
    t->entries[fd] = f;
    return fd;
}

int fd_close(struct fd_table *t, int fd) {
    if (!t || fd < 0 || fd >= FD_MAX || !t->entries[fd]) {
        return -SYS_EBADF;
    }
    file_unref(t->entries[fd]);
    t->entries[fd] = NULL;
    return 0;
}

struct file *fd_get(struct fd_table *t, int fd) {
    if (!t || fd < 0 || fd >= FD_MAX) {
        return NULL;
    }
    return t->entries[fd];
}
