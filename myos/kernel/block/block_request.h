/* Block request model: a uniform descriptor for a transfer. v0 executes
 * synchronously through the cached block_read/block_write; the structure is the
 * foundation for an async request queue later.
 *
 * The async layer (block_request_submit_async) is a real foundation: it enqueues
 * a request carrying a completion callback, then drains the queue, executing each
 * request and invoking its callback with the final status. Execution is still
 * synchronous (no IRQ-driven completion yet), but the queue + callback dispatch
 * path is exactly the one an interrupt-completed driver will reuse. */
#ifndef MYOS_BLOCK_REQUEST_H
#define MYOS_BLOCK_REQUEST_H

#include "types.h"

struct block_device;
struct block_request;

enum block_op { BLOCK_OP_READ = 0, BLOCK_OP_WRITE = 1 };

/* Completion callback: invoked once the request has finished. `status` mirrors
 * req->status (0 = ok, negative = error). `cookie` is the caller's context. */
typedef void (*block_completion_fn)(struct block_request *req, int status, void *cookie);

struct block_request {
    struct block_device *dev;
    enum block_op        op;
    uint64_t             lba;
    uint32_t             count;
    void                *buffer;
    int                  status;     /* 0 = ok, negative = error */

    /* Async completion (ignored by the synchronous block_request_submit). */
    block_completion_fn  on_complete;
    void                *cookie;
    struct block_request *next;      /* queue link */
};

/* Synchronous submit: run the transfer immediately, return its status. */
int block_request_submit(struct block_request *req);

/* Async submit: enqueue `req` (which carries on_complete/cookie) and drain the
 * queue, executing each request and dispatching its completion callback. Returns
 * 0 if the request was accepted, negative on bad arguments. The final transfer
 * status is delivered to the callback and also stored in req->status. */
int block_request_submit_async(struct block_request *req);

#endif /* MYOS_BLOCK_REQUEST_H */
