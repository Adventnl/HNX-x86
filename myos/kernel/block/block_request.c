/* Synchronous block request execution. */
#include "block_request.h"
#include "block_device.h"
#include "syscall_numbers.h"

int block_request_submit(struct block_request *req) {
    if (!req || !req->dev) {
        return -SYS_EINVAL;
    }
    if (req->op == BLOCK_OP_WRITE) {
        req->status = block_write(req->dev, req->lba, req->buffer, req->count);
    } else {
        req->status = block_read(req->dev, req->lba, req->buffer, req->count);
    }
    return req->status;
}
