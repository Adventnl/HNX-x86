/* Character device: an unstructured byte stream (console, null, zero). Block
 * devices arrive in Prompt 5. */
#ifndef MYOS_DEVICE_CHAR_DEVICE_H
#define MYOS_DEVICE_CHAR_DEVICE_H

#include "types.h"

struct char_device {
    const char *name;
    int64_t (*read)(struct char_device *cd, void *buf, uint64_t size);
    int64_t (*write)(struct char_device *cd, const void *buf, uint64_t size);
    void *priv;
};

int64_t char_device_read(struct char_device *cd, void *buf, uint64_t size);
int64_t char_device_write(struct char_device *cd, const void *buf, uint64_t size);

/* Built-in pseudo-devices. */
struct char_device *char_device_null(void);
struct char_device *char_device_zero(void);

#endif /* MYOS_DEVICE_CHAR_DEVICE_H */
