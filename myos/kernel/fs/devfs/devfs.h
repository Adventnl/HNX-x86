/* devfs: a synthetic filesystem exposing the character-device registry under
 * /dev (console, null, zero). */
#ifndef MYOS_FS_DEVFS_H
#define MYOS_FS_DEVFS_H

struct filesystem;

/* Build the devfs filesystem (mount it at "/dev"). Logs "[OK] devfs online". */
struct filesystem *devfs_create(void);

#endif /* MYOS_FS_DEVFS_H */
