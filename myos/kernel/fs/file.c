/* Open-file lifecycle. */
#include "file.h"
#include "heap.h"
#include "string.h"

struct file *file_alloc(struct vnode *vn, int flags, const char *abspath) {
    struct file *f = (struct file *)kcalloc(1, sizeof(*f));
    if (!f) {
        return NULL;
    }
    f->vnode = vn;
    f->offset = 0;
    f->flags = flags;
    f->refcount = 1;
    if (abspath) {
        strlcpy(f->path, abspath, sizeof(f->path));
    }
    return f;
}

void file_ref(struct file *f) {
    if (f) {
        f->refcount++;
    }
}

void file_unref(struct file *f) {
    if (!f) {
        return;
    }
    if (--f->refcount <= 0) {
        kfree(f);   /* the vnode belongs to the filesystem; not freed here */
    }
}
