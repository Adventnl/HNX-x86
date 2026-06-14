/* Absolute-path helpers: normalization, basename, parent. Paths are POSIX-style
 * ('/'-separated, '/' root). No symlinks. */
#ifndef MYOS_FS_PATH_H
#define MYOS_FS_PATH_H

#include "types.h"

/* Resolve `path` (absolute or relative to `cwd`) into a normalized absolute path
 * in `out`. Collapses ".", "..", and repeated '/'. Always begins with '/'.
 * Returns 0 on success, negative on overflow / bad input. */
int path_resolve(const char *cwd, const char *path, char *out, uint64_t out_size);

/* Pointer to the final component of an absolute path ("/" -> ""). */
const char *path_basename(const char *path);

/* Parent directory of `abspath` into `out` ("/x" -> "/", "/" -> "/"). */
int path_parent(const char *abspath, char *out, uint64_t out_size);

#endif /* MYOS_FS_PATH_H */
