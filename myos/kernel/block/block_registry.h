/* Block device registry + block layer bring-up. */
#ifndef MYOS_BLOCK_REGISTRY_H
#define MYOS_BLOCK_REGISTRY_H

#include "block_device.h"

/* Initialize the block layer (registry + cache). Logs "[OK] Block layer online". */
void block_init(void);

int block_register_device(struct block_device *device);
struct block_device *block_get_device(const char *name);
void block_dump_devices(void);

int block_device_count(void);
struct block_device *block_device_at(int index);

#endif /* MYOS_BLOCK_REGISTRY_H */
