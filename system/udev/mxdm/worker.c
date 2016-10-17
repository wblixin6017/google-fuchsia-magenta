// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides the worker thread for MXDM.  The worker manages the bitmap
// and cache, and is tasked with performing the devie initializtion and
// teardown.  It tries to minimze shared state that requires synchronization,
// limiting it to just two attributes: the worker's state variable and I/O
// transaction queue.  It abstracts these behind functions that it makes
// available to device code to use when handling callbacks from devmgr.

#define MXDM_IMPLEMENTATION

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>
#include <threads.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/iotxn.h>
#include <magenta/device/block.h>
#include <magenta/fuchsia-types.h>
#include <magenta/listnode.h>
#include <magenta/types.h>

#include "bitmap.h"
#include "cache.h"
#include "common.h"
#include "device.h"
#include "mxdm.h"
#include "worker.h"

// Types

// Represents the state of a worker.
typedef enum mxdm_worker_state {
    kWorkerWorking,
    kWorkerStopping,
    kWorkerExiting,
} mxdm_worker_state_t;

// Control structure for the worker thread.
struct mxdm_worker {
    // The associated MXDM device.
    mxdm_device_t* device;
    // Mutex used to synchronize the worker's state and queue.
    mtx_t mtx;
    // Condition variable used to signal the worker.
    cnd_t cnd;
    // The current state of the worker thread.
    mxdm_worker_state_t state;
    // A queue of I/O transactions that the worker will process.
    list_node_t queue;
    // A list of external I/O transactions waiting for the worker to complete
    // them.
    list_node_t txns;
    // A bitmap over the blocks of the device.
    mxdm_bitmap_t* bitmap;
    // A cache of metadata blocks
    mxdm_cache_t* cache;
    // Callbacks to the implementing driver.
    mxdm_worker_ops_t ops;
    // Offset of the first data block.
    uint64_t data_blkoff;
    // Number of data blocks.
    uint64_t data_blklen;
};

// I/O transaction information that is passed to its completion callback.
typedef struct mxdm_txn_cookie {
    // Handle to the worker control structure.
    mxdm_worker_t* worker;
    // Represents the origin of this internal I/O transaction, either a
    // cache-miss on a block or an external I/O transaction.
    union {
        mxdm_block_t* block;
        iotxn_t* txn;
    } origin;
    // The starting block (inclusive) of the I/O transaction.  This value is
    // updated by the I/O callbacks to track progress when an I/O transaction
    // requires more processing than can be done at once.
    uint64_t blkoff;
    // Ending block (exclusive) of the I/O transaction.
    uint64_t blkmax;
} mxdm_txn_cookie_t;

// Forward declarations

// Performs the asynchronous portion of the device setup.  Anything that might
// cause mxdm_init to take more than a trivial amount of time is moved to this
// function on the worker thread.
static mx_status_t mxdm_worker_init(mxdm_init_info_t* info,
                                    mxdm_worker_t** out);

// Releases all of the resources associated with this worker.
static void mxdm_worker_free(mxdm_worker_t* worker);

// Processes iotxns from the worker queue.
static mx_status_t mxdm_worker_loop(mxdm_worker_t* worker);

// Creates an internal transaction clone from an external transaction, allowing
// the worker to own the txn and mangle it as needed.
static iotxn_t* mxdm_clone_txn(mxdm_worker_t* worker, iotxn_t* txn);

// Always returns kMxdmContinueTxn.  This callback is used if none was given in
// mxdm_init.
static mxdm_txn_action_t mxdm_default_before(mxdm_worker_t* worker,
                                             iotxn_t* txn, uint64_t* blkoff,
                                             uint64_t blkmax);

// Places the transaction back onto the worker's queue after the I/O is
// complete.
static void mxdm_iotxn_cb(iotxn_t* txn, void* cookie);

// Always returns kMxdmCompleteTxn.  This callback is used if none was given in
// mxdm_init.
static mxdm_txn_action_t mxdm_default_after(mxdm_worker_t* worker, iotxn_t* txn,
                                            uint64_t* blkoff, uint64_t blkmax);

// Releases resources for internal I/O transactions, and calls the completion
// callback for external transactions.
static void mxdm_complete_txn(mxdm_worker_t* worker, iotxn_t* txn);

// Public functions

const mxdm_device_t* mxdm_worker_get_device(mxdm_worker_t* worker) {
    MXDM_IF_NULL(worker, return NULL);
    return worker->device;
}

void* mxdm_worker_get_context(mxdm_worker_t* worker) {
    MXDM_IF_NULL(worker, return NULL);
    return mxdm_device_get_context(worker->device);
}

bool mxdm_is_data_block(const mxdm_worker_t* worker, uint64_t blkoff) {
    MXDM_IF_NULL(worker, return false);
    return worker->data_blkoff <= blkoff &&
           blkoff < worker->data_blkoff + worker->data_blklen;
}

mx_status_t mxdm_acquire_block(mxdm_worker_t* worker, uint64_t blkoff,
                               mxdm_block_t** out) {
    MXDM_IF_NULL(worker, return ERR_INVALID_ARGS);
    MXDM_IF_NULL(out, return ERR_INVALID_ARGS);
    return mxdm_cache_acquire(worker->cache, blkoff, out);
}

void mxdm_release_block(mxdm_worker_t* worker, mxdm_block_t* block) {
    MXDM_IF_NULL(worker, return );
    MXDM_IF_NULL(block, return );
    mxdm_cache_release(worker->cache, block);
}

bool mxdm_check_block(mxdm_worker_t* worker, uint64_t blkoff) {
    MXDM_IF_NULL(worker, return false);
    return mxdm_check_blocks(worker, &blkoff, blkoff + 1);
}

bool mxdm_check_blocks(mxdm_worker_t* worker, uint64_t* blkoff,
                       uint64_t blkmax) {
    MXDM_IF_NULL(worker, return false);
    MXDM_IF_NULL(blkoff, return false);
    return mxdm_bitmap_get(worker->bitmap, blkoff, blkmax);
}

mx_status_t mxdm_mark_block(mxdm_worker_t* worker, uint64_t blkoff) {
    MXDM_IF_NULL(worker, return ERR_INVALID_ARGS);
    return mxdm_bitmap_set(worker->bitmap, blkoff);
}

mx_status_t mxdm_clear_blocks(mxdm_worker_t* worker, uint64_t blkoff,
                              uint64_t blkmax) {
    MXDM_IF_NULL(worker, return ERR_INVALID_ARGS);
    return mxdm_bitmap_clr(worker->bitmap, blkoff, blkmax);
}

// Protected functions

int mxdm_worker(void* arg) {
    assert(arg);
    mxdm_init_info_t* info = arg;
    mxdm_worker_t* worker = NULL;
    mx_status_t rc = mxdm_worker_init(info, &worker);
    if (rc < 0) {
        driver_unbind(info->drv, info->parent);
    }
    free(info);
    if (rc == NO_ERROR) {
        rc = mxdm_worker_loop(worker);
    }
    mxdm_worker_free(worker);
    return rc;
}

mx_off_t mxdm_worker_data_size(mxdm_worker_t* worker) {
    assert(worker);
    return worker->data_blklen * MXDM_BLOCK_SIZE;
}

mx_status_t mxdm_worker_set_cb(mxdm_worker_t* worker, iotxn_t* txn,
                               void* origin) {
    assert(worker);
    assert(txn);
    assert(origin);
    txn->complete_cb = mxdm_iotxn_cb;
    // TODO(aarongreen): This is another place where a growable pool might be
    // better.
    mxdm_txn_cookie_t* c = calloc(1, sizeof(mxdm_txn_cookie_t));
    if (!c) {
        MXDM_TRACE("out of memory!");
        return ERR_NO_MEMORY;
    }
    c->worker = worker;
    // Is this a cache load, or a cloned read?
    if (!mxdm_is_data_block(worker, txn->offset / MXDM_BLOCK_SIZE) &&
        txn->length == MXDM_BLOCK_SIZE) {
        c->origin.block = origin;
    } else {
        c->origin.txn = origin;
    }
    c->blkoff = txn->offset / MXDM_BLOCK_SIZE;
    c->blkmax = ((txn->offset + txn->length - 1) / MXDM_BLOCK_SIZE) + 1;
    txn->cookie = c;
    return NO_ERROR;
}

void mxdm_worker_stop(mxdm_worker_t* worker) {
    assert(worker);
    mtx_lock(&worker->mtx);
    worker->state = kWorkerStopping;
    cnd_broadcast(&worker->cnd);
    mtx_unlock(&worker->mtx);
}

void mxdm_worker_exit(mxdm_worker_t* worker) {
    assert(worker);
    mtx_lock(&worker->mtx);
    worker->state = kWorkerExiting;
    cnd_broadcast(&worker->cnd);
    mtx_unlock(&worker->mtx);
}

void mxdm_worker_queue(mxdm_worker_t* worker, iotxn_t* txn) {
    assert(worker);
    assert(txn);
    bool queued = false;
    bool is_data = mxdm_is_data_block(worker, txn->offset / MXDM_BLOCK_SIZE);
    mtx_lock(&worker->mtx);
    if (worker->state == kWorkerWorking) {
        // Prioritize metadata requests
        if (is_data) {
            list_add_tail(&worker->queue, &txn->node);
        } else {
            list_add_head(&worker->queue, &txn->node);
        }
        cnd_broadcast(&worker->cnd);
        queued = true;
    }
    mtx_unlock(&worker->mtx);
    if (!queued) {
        txn->status = ERR_HANDLE_CLOSED;
        mxdm_complete_txn(worker, txn);
    }
}

// Private functions

static mx_status_t mxdm_worker_init(mxdm_init_info_t* info,
                                    mxdm_worker_t** out) {
    assert(info);
    assert(out);
    mx_status_t rc = NO_ERROR;
    mxdm_worker_t* worker = calloc(1, sizeof(mxdm_worker_t));
    if (!worker) {
        MXDM_TRACE("out of memory!");
        goto fail;
    }
    // Check block related sizes on parent.
    mx_device_t* parent = info->parent;
    size_t size = 0;
    rc = parent->ops->ioctl(parent, IOCTL_BLOCK_GET_BLOCKSIZE, NULL, 0, &size,
                            sizeof(size));
    if (rc < 0) {
        MXDM_TRACE("get_blocksize ioctl failed: %d", rc);
        goto fail;
    }
    if (MXDM_BLOCK_SIZE % size != 0) {
        MXDM_TRACE("invalid parent block size: %zu", size);
        rc = ERR_NOT_SUPPORTED;
        goto fail;
    }
    size = parent->ops->get_size(parent);
    if (size == 0) {
        MXDM_TRACE("parent device is not seekable: %s", parent->name);
        rc = ERR_NOT_SUPPORTED;
        goto fail;
    }
    // Set up the worker.
    worker->device = info->device;
    if (mtx_init(&worker->mtx, mtx_plain) != thrd_success ||
        cnd_init(&worker->cnd) != thrd_success) {
        MXDM_TRACE("failed to init worker");
        rc = ERR_NO_RESOURCES;
        goto fail;
    }
    worker->state = kWorkerWorking;
    list_initialize(&worker->queue);
    list_initialize(&worker->txns);
    rc = mxdm_bitmap_init(size / MXDM_BLOCK_SIZE, &worker->bitmap);
    if (rc < 0) {
        goto fail;
    }
    rc = mxdm_cache_init(worker, &worker->cache);
    if (rc < 0) {
        goto fail;
    }
    worker->ops.prepare = info->ops->prepare;
    worker->ops.release = info->ops->release;
    worker->ops.before_read =
        (info->ops->before_read ? info->ops->before_read : mxdm_default_before);
    worker->ops.before_write =
        (info->ops->before_write ? info->ops->before_write
                                 : mxdm_default_before);
    worker->ops.after_read =
        (info->ops->after_read ? info->ops->after_read : mxdm_default_after);
    worker->ops.after_write =
        (info->ops->after_write ? info->ops->after_write : mxdm_default_after);
    // Next, configure the device in devmgr. Lock the worker so no one can add
    // txns to the queue until we're ready.
    mtx_lock(&worker->mtx);
    rc = mxdm_device_init(worker, info);
    // Use the "prepare" callback to do any asynchronous set-up.
    if (rc == NO_ERROR) {
        rc = worker->ops.prepare(worker, size / MXDM_BLOCK_SIZE,
                                 &worker->data_blkoff, &worker->data_blklen);
    }
    // This prevents any other threads from queuing a txn after we release mtx.
    if (rc < 0) {
        worker->state = kWorkerExiting;
    }
    mtx_unlock(&worker->mtx);
    if (rc < 0) {
        goto fail;
    }
done:
    *out = worker;
    return NO_ERROR;
fail:
    mxdm_worker_free(worker);
    return rc;
}

static void mxdm_worker_free(mxdm_worker_t* worker) {
    if (!worker) {
        return;
    }
    mxdm_bitmap_free(worker->bitmap);
    mxdm_cache_free(worker->cache);
    mxdm_device_free(worker->device);
    free(worker);
}

static mx_status_t mxdm_worker_loop(mxdm_worker_t* worker) {
    assert(worker);
    iotxn_t* txn;
    mxdm_worker_state_t state;
    size_t counter = 0;
    while (true) {
        mtx_lock(&worker->mtx);
        while (true) {
            txn = list_remove_head_type(&worker->queue, iotxn_t, node);
            state = worker->state;
            MXDM_TRACE("worker state is %d, txn is %s", state,
                       (txn ? "not NULL" : "NULL"));
            // Is there work to do?
            if (txn || state == kWorkerExiting) {
                break;
            }
            MXDM_TRACE("waiting in loop");
            cnd_wait(&worker->cnd, &worker->mtx);
            MXDM_TRACE("worker signalled");
        }
        mtx_unlock(&worker->mtx);
        // Handle any non-working or error states.
        switch (state) {
        case kWorkerExiting:
            if (!txn) {
                return worker->ops.release(worker);
            }
        // Fall-through
        case kWorkerStopping:
            if (txn->status >= 0) {
                txn->status = ERR_HANDLE_CLOSED;
            }
        // Fall-through
        case kWorkerWorking:
        default:
            if (txn->status < 0) {
                mxdm_complete_txn(worker, txn);
                continue;
            }
            break;
        }
        // If this is an external (not cloned) iotxn, clone it.
        if (txn->context == worker->device) {
            txn = mxdm_clone_txn(worker, txn);
            if (!txn) {
                // mxdm_clone_txn calls mxdm_complete_txn on error.
                continue;
            }
        }
        mxdm_txn_cookie_t* c = txn->cookie;
        mxdm_txn_action_t next = kMxdmIgnoreTxn;
        MXDM_TRACE("processing iotxn: blkoff=%lu, blkmax=%lu", c->blkoff,
                   c->blkmax);
        if (txn->actual == 0) {
            // I/O hasn't occurred yet.
            if (txn->opcode == IOTXN_OP_READ) {
                next =
                    worker->ops.before_read(worker, txn, &c->blkoff, c->blkmax);
            } else {
                next = worker->ops.before_write(worker, txn, &c->blkoff,
                                                c->blkmax);
            }
        } else {
            if (txn->opcode == IOTXN_OP_READ) {
                next =
                    worker->ops.after_read(worker, txn, &c->blkoff, c->blkmax);
            } else {
                next =
                    worker->ops.after_write(worker, txn, &c->blkoff, c->blkmax);
            }
        }
        MXDM_TRACE("iotxn processed: blkoff=%lu, blkmax=%lu", c->blkoff,
                   c->blkmax);
        switch (next) {
        case kMxdmIgnoreTxn:
            // Something else is handling the txn.
            break;
        case kMxdmRequeueTxn:
            mtx_lock(&worker->mtx);
            list_add_tail(&worker->queue, &txn->node);
            mtx_unlock(&worker->mtx);
            break;
        case kMxdmContinueTxn:
            assert(txn->actual == 0);
            c->blkoff = txn->offset / MXDM_BLOCK_SIZE;
            mxdm_device_queue(worker->device, txn);
            break;
        case kMxdmCompleteTxn:
        default:
            mxdm_complete_txn(worker, txn);
            break;
        }
        // Periodically reclaim memory from the bitmaps.
        counter = (counter + 1) & 0xFFFF;
        if (counter == 0) {
            mxdm_bitmap_compress(worker->bitmap);
        }
    }
}

static iotxn_t* mxdm_clone_txn(mxdm_worker_t* worker, iotxn_t* txn) {
    assert(worker);
    assert(txn);
    assert(txn->context == worker->device);
    size_t data_offset = worker->data_blkoff * MXDM_BLOCK_SIZE;
    size_t data_length = worker->data_blklen * MXDM_BLOCK_SIZE;
    if (txn->offset % MXDM_BLOCK_SIZE != 0 ||
        txn->length % MXDM_BLOCK_SIZE != 0 || txn->offset >= data_length) {
        MXDM_TRACE("invalid txn: offset=%lu, length=%lu", txn->offset,
                   txn->length);
        txn->status = ERR_INVALID_ARGS;
        mxdm_complete_txn(worker, txn);
        return NULL;
    }
    // Clone the txn and take ownership of it.
    iotxn_t* cloned = NULL;
    txn->status = txn->ops->clone(txn, &cloned, 0);
    if (txn->status != NO_ERROR) {
        MXDM_TRACE("clone returned %d", txn->status);
        mxdm_complete_txn(worker, txn);
        return NULL;
    }
    list_add_tail(&worker->txns, &txn->node);
    // Convert offset and length to underlying device.
    cloned->context = NULL;
    cloned->length = MIN(data_length - cloned->offset, cloned->length);
    cloned->offset += data_offset + cloned->offset;
    cloned->status = mxdm_worker_set_cb(worker, cloned, txn);
    if (cloned->status != NO_ERROR) {
        mxdm_complete_txn(worker, cloned);
        return NULL;
    }
    return cloned;
}

static mxdm_txn_action_t mxdm_default_before(mxdm_worker_t* worker,
                                             iotxn_t* txn, uint64_t* blkoff,
                                             uint64_t blkmax) {
    assert(worker);
    assert(txn);
    assert(blkoff);
    *blkoff = blkmax;
    return kMxdmContinueTxn;
}

static void mxdm_iotxn_cb(iotxn_t* txn, void* cookie) {
    assert(txn);
    assert(cookie);
    mxdm_txn_cookie_t* c = cookie;
    mxdm_worker_t* worker = c->worker;
    mxdm_worker_queue(worker, txn);
}

static mxdm_txn_action_t mxdm_default_after(mxdm_worker_t* worker, iotxn_t* txn,
                                            uint64_t* blkoff, uint64_t blkmax) {
    assert(worker);
    assert(txn);
    assert(blkoff);
    *blkoff = blkmax;
    return kMxdmCompleteTxn;
}

static void mxdm_complete_txn(mxdm_worker_t* worker, iotxn_t* txn) {
    assert(worker);
    assert(txn);
    if (txn->context == worker->device) {
        // txn is an original transaction.
        MXDM_TRACE("completing external iotxn for data block %lu",
                   txn->offset / MXDM_BLOCK_SIZE);
        txn->context = NULL;
        if (list_in_list(&txn->node)) {
            list_delete(&txn->node);
        }
        txn->ops->complete(txn, txn->status, txn->actual);
        return;
    }
    mxdm_txn_cookie_t* c = txn->cookie;
    txn->cookie = NULL;
    if (mxdm_is_data_block(worker, txn->offset / MXDM_BLOCK_SIZE)) {
        // txn is a clone, cookie has the external txn.
        MXDM_TRACE("completing cloned iotxn for raw block %lu",
                   txn->offset / MXDM_BLOCK_SIZE);
        iotxn_t* cloned = txn;
        txn = c->origin.txn;
        txn->status = cloned->status;
        txn->actual = cloned->actual;
        cloned->ops->release(cloned);
        mxdm_complete_txn(worker, txn);
    } else {
        // txn is a cache-load, cookie has the cache block.
        MXDM_TRACE("completing cache iotxn for metadata block %lu",
                   txn->offset / MXDM_BLOCK_SIZE);
        mxdm_cache_process(c->origin.block, txn, worker);
    }
    free(c);
}

#undef MXDM_IMPLEMENTATION
