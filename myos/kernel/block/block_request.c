/* Block request execution: synchronous submit + an async queue foundation.
 *
 * The async path enqueues requests on a FIFO and drains them in submission
 * order, executing each transfer and dispatching its completion callback. The
 * transfer itself is still synchronous, but the enqueue/drain/dispatch structure
 * is the real one a future IRQ-completed driver will plug into. */
#include "block_request.h"
#include "block_device.h"
#include "syscall_numbers.h"

/* FIFO of pending async requests. */
static struct block_request *g_queue_head;
static struct block_request *g_queue_tail;
static int                   g_draining;   /* re-entrancy guard for the drain */

static int request_execute(struct block_request *req) {
    if (req->op == BLOCK_OP_WRITE) {
        req->status = block_write(req->dev, req->lba, req->buffer, req->count);
    } else {
        req->status = block_read(req->dev, req->lba, req->buffer, req->count);
    }
    return req->status;
}

int block_request_submit(struct block_request *req) {
    if (!req || !req->dev) {
        return -SYS_EINVAL;
    }
    return request_execute(req);
}

/* Execute every queued request in FIFO order, invoking each completion callback
 * after the transfer finishes. Guarded against re-entrant submission from within
 * a callback (a callback that submits more work simply extends the queue). */
static void block_request_drain(void) {
    if (g_draining) {
        return;
    }
    g_draining = 1;
    while (g_queue_head) {
        struct block_request *req = g_queue_head;
        g_queue_head = req->next;
        if (!g_queue_head) {
            g_queue_tail = NULL;
        }
        req->next = NULL;

        int status = request_execute(req);
        if (req->on_complete) {
            req->on_complete(req, status, req->cookie);
        }
    }
    g_draining = 0;
}

int block_request_submit_async(struct block_request *req) {
    if (!req || !req->dev) {
        return -SYS_EINVAL;
    }
    req->status = 0;
    req->next = NULL;

    /* Enqueue at the tail. */
    if (g_queue_tail) {
        g_queue_tail->next = req;
    } else {
        g_queue_head = req;
    }
    g_queue_tail = req;

    block_request_drain();
    return 0;
}
