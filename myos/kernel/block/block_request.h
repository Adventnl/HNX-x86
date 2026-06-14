/* Block request model: a uniform descriptor for a transfer. v0 executes
 * synchronously through the cached block_read/block_write; the structure is the
 * foundation for an async request queue later. */
#ifndef MYOS_BLOCK_REQUEST_H
#define MYOS_BLOCK_REQUEST_H

#include "types.h"

struct block_device;

enum block_op { BLOCK_OP_READ = 0, BLOCK_OP_WRITE = 1 };

struct block_request {
    struct block_device *dev;
    enum block_op        op;
    uint64_t             lba;
    uint32_t             count;
    void                *buffer;
    int                  status;     /* 0 = ok, negative = error */
};

int block_request_submit(struct block_request *req);

#endif /* MYOS_BLOCK_REQUEST_H */
