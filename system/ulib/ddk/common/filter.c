// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO: add error messages
// TODO: work out dev vs filter vs verity-likes

// TODO: move me
#include <ddk/common/filter.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/ioctl.h>
#include <ddk/iotxn.h>
#include <magenta/types.h>

#include <assert.h>
#include <magenta/listnode.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>

// Types

typedef enum filter_mode {
    kFilterModeInitialized,
    kFilterModeRunning,
    kFilterModeStopped,
    kFilterModeReleased,
} filter_mode_t;

struct filter {
    mx_device_t dev;
    filter_mode_t mode;
    list_node_t iotxns;
    mtx_t mtx;
    cnd_t cnd;
    list_node_t workers;
    filter_worker_t* default_worker;
    uint8_t ioctl_family;
    const filter_ops_t *ops;
};

struct filter_worker {
    filter_t *filter;
    size_t num;
    thrd_t* threads;
    filter_worker_f func;
    void* arg;
    list_node_t queue;
    mtx_t mtx;
    cnd_t cnd;
    list_node_t node;
    bool stop;
};

// General utility subroutines

filter_t* filter_get(mx_device_t* dev) {
    return containerof(dev, filter_t, dev);
}

mx_device_t* filter_dev(filter_t* filter) {
    return &filter->dev;
}

filter_t* filter_from_cloned(iotxn_t *cloned) {
    iotxn_t *txn = cloned->cookie;
    filter_worker_t *worker = txn->context;
    return worker->filter;
}

static filter_mode_t filter_get_mode(filter_t* filter) {
    filter_mode_t mode;
    mtx_lock(&filter->mtx);
    mode = filter->mode;
    mtx_unlock(&filter->mtx);
    return mode;
}

static void filter_set_mode(filter_t* filter, filter_mode_t mode) {
    mtx_lock(&filter->mtx);
    filter->mode = mode;
    if (mode == kFilterModeReleased) {
        cnd_broadcast(&filter->cnd);
    }
    mtx_unlock(&filter->mtx);
}

// Worker subroutines

static int filter_worker_thread(void* args) {
    filter_worker_t* worker = args;
    while (true) {
        // Get the next read response to be digested.
        mtx_lock(&worker->mtx);
        while (list_is_empty(&worker->queue) && !worker->stop) {
            cnd_wait(&worker->cnd, &worker->mtx);
        }
        bool stopped = worker->stop;
        iotxn_t* cloned = list_remove_head_type(&worker->queue, iotxn_t, node);
        mtx_unlock(&worker->mtx);
        if (stopped && !cloned) {
            thrd_exit(EXIT_SUCCESS);
        }
        if (stopped) {
            cloned->status = ERR_HANDLE_CLOSED;
            filter_complete(cloned);
        } else {
            worker->func(cloned);
        }
    }
    return 0;
}

static void filter_worker_stop(filter_worker_t* worker) {
    if (!worker) {
        return;
    }
    if (worker->num > 0) {
        mtx_lock(&worker->mtx);
        worker->stop = true;
        cnd_broadcast(&worker->cnd);
        mtx_unlock(&worker->mtx);
        int result;
        for (size_t i = 0; i < worker->num; ++i) {
            thrd_join(worker->threads[i], &result);
        }
    }
    if (worker->threads) {
        free(worker->threads);
    }
    free(worker);
}

static filter_worker_t* filter_worker_start(filter_worker_f func, size_t num) {
    filter_worker_t* worker = calloc(1, sizeof(filter_worker_t));
    if (!worker) {
        goto fail;
    }
    worker->threads = calloc(num, sizeof(thrd_t));
    if (!worker->threads) {
        goto fail;
    }
    if (mtx_init(&worker->mtx, mtx_plain) != thrd_success ||
        cnd_init(&worker->cnd) != thrd_success) {
        goto fail;
    }
    worker->func = func;
    list_initialize(&worker->queue);
    worker->stop = false;
    for (size_t i = 0; i < num; ++i) {
        if (thrd_create(&worker->threads[i], filter_worker_thread, worker) != thrd_success) {
            goto fail;
        }
    }
    worker->num = num;
    return worker;
fail:
    filter_worker_stop(worker);
    return NULL;
}

static void filter_worker_enqueue(filter_worker_t* worker, iotxn_t* cloned) {
    bool queued = false;
    mtx_lock(&worker->mtx);
    if (!worker->stop) {
        list_add_tail(&worker->queue, &cloned->node);
        cnd_broadcast(&worker->cnd);
        queued = true;
    }
    mtx_unlock(&worker->mtx);
    if (!queued) {
        cloned->status = ERR_HANDLE_CLOSED;
        filter_complete(cloned);
    }
}

// Protocol support subroutines

static int filter_releaser(void* args) {
    filter_t* filter = args;
    filter_worker_t* work = NULL;
    filter_worker_t* temp = NULL;
    list_for_every_entry_safe (&filter->workers, work, temp, filter_worker_t, node) {
        list_delete(&work->node);
        filter_worker_stop(work);
    }
    mtx_lock(&filter->mtx);
    while (!list_is_empty(&filter->iotxns) || filter->mode != kFilterModeReleased) {
        cnd_wait(&filter->cnd, &filter->mtx);
    }
    mtx_unlock(&filter->mtx);
    mx_status_t rc = NO_ERROR;
    if (filter->ops->release) {
        rc = filter->ops->release(filter);
    }
    free(filter);
    thrd_exit(rc == NO_ERROR ? 0 : 1);
}

static void filter_cb(iotxn_t* cloned, void* cookie) {
    iotxn_t* txn = cookie;
    filter_worker_t* worker = txn->context;
    filter_t* filter = worker->filter;
    if (filter_get_mode(filter) != kFilterModeRunning) {
        cloned->status = ERR_HANDLE_CLOSED;
        filter_complete(cloned);
        return;
    }
    if (cloned->status != NO_ERROR) {
        filter_complete(cloned);
        return;
    }
    if (cloned->opcode == IOTXN_OP_READ) {
        filter_worker_enqueue(worker, cloned);
    } else {
        filter_complete(cloned);
    }
}

void filter_assign(iotxn_t* txn, filter_worker_t* worker, bool skip_validation) {
    mx_status_t rc = NO_ERROR;
    iotxn_t* cloned = NULL;
    // Check to make sure we're still alive.
    filter_t* filter = worker->filter;
    if (filter_get_mode(filter) != kFilterModeRunning) {
        rc = ERR_HANDLE_CLOSED;
        goto fail;
    }
    // Clone the txn and take ownership of it.
    rc = txn->ops->clone(txn, &cloned, 0);
    if (rc != NO_ERROR) {
        goto fail;
    }
    cloned->complete_cb = filter_cb;
    cloned->cookie = txn;
    txn->context = worker;
    mtx_lock(&filter->mtx);
    list_add_tail(&filter->iotxns, &txn->node);
    mtx_unlock(&filter->mtx);
    // Validate the cloned txn, if needed.
    rc = (skip_validation ? NO_ERROR : filter->ops->validate_iotxn(cloned));
    if (rc != NO_ERROR) {
        goto fail;
    }
    if (cloned->opcode == IOTXN_OP_WRITE || cloned->actual != 0) {
        filter_worker_enqueue(worker, cloned);
    } else {
        filter_continue(cloned);
    }
    return;
fail:
    if (cloned) {
        cloned->status = rc;
        filter_complete(cloned);
    } else {
        txn->ops->complete(txn, rc, 0);
    }
}

void filter_continue(iotxn_t *cloned) {
    iotxn_t *txn = cloned->cookie;
    filter_worker_t *worker = txn->context;
    filter_t* filter = worker->filter;
    mx_device_t* parent = filter->dev.parent;
    parent->ops->iotxn_queue(parent, cloned);
}

void filter_complete(iotxn_t* cloned) {
    iotxn_t* txn = cloned->cookie;
    filter_worker_t* worker = txn->context;
    filter_t* filter = worker->filter;
    mtx_lock(&filter->mtx);
    list_delete(&txn->node);
    if (list_is_empty(&filter->iotxns)) {
        cnd_broadcast(&filter->cnd);
    }
    mtx_unlock(&filter->mtx);
    txn->context = NULL;
    if (cloned->status != NO_ERROR) {
        cloned->actual = 0;
    }
    txn->ops->complete(txn, cloned->status, cloned->actual);
    cloned->ops->release(cloned);
}

// Protocol subroutines

static void filter_unbind(mx_device_t* dev) {
    filter_t* filter = filter_get(dev);
    filter_set_mode(filter, kFilterModeStopped);
    thrd_t thrd;
    if (thrd_create(&thrd, filter_releaser, filter) != thrd_success) {
        return;
    }
    if (thrd_detach(thrd) != thrd_success) {
        return;
    }
}

static mx_status_t filter_release(mx_device_t* dev) {
    filter_t* filter = filter_get(dev);
    mx_device_t* child = NULL;
    mx_device_t* temp = NULL;
    list_for_every_entry_safe (&dev->children, child, temp, mx_device_t, node) {
        device_remove(child);
    }
    filter_set_mode(filter, kFilterModeReleased);
    return NO_ERROR;
}

static void filter_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    filter_t* filter = filter_get(dev);
    filter_assign(txn, filter->default_worker, false /* don't skip_validation */);
}

static mx_off_t filter_get_size(mx_device_t* dev) {
    filter_t* filter = filter_get(dev);
    mx_device_t* parent = dev->parent;
    mx_off_t size = parent->ops->get_size(parent);
    if (filter->ops->get_size) {
        size = filter->ops->get_size(filter, size);
    }
    return size;
}

static ssize_t filter_ioctl(mx_device_t* dev, uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max) {
    filter_t* filter = filter_get(dev);
    ssize_t rc = ERR_NOT_SUPPORTED;
    if (filter->ops->ioctl) {
        rc = filter->ops->ioctl(filter, op, cmd, cmdlen, reply, max);
    }
    if (rc == ERR_NOT_SUPPORTED) {
        mx_device_t* parent = dev->parent;
        rc = parent->ops->ioctl(parent, op, cmd, cmdlen, reply, max);
    }
    return rc;
}

static mx_protocol_device_t filter_proto = {
    .unbind = filter_unbind,
    .release = filter_release,
    .iotxn_queue = filter_iotxn_queue,
    .get_size = filter_get_size,
    .ioctl = filter_ioctl,
};

// Bind subroutines

filter_t* filter_init(mx_driver_t* drv,
                      const char* name,
                      uint32_t protocol_id,
                      const filter_ops_t* ops) {
    filter_t* filter = calloc(1, sizeof(filter_t));
    if (!filter) {
        goto fail;
    }
    device_init(&filter->dev, drv, name, &filter_proto);
    list_initialize(&filter->iotxns);
    if (!mtx_init(&filter->mtx, mtx_plain) || !cnd_init(&filter->cnd)) {
        goto fail;
    }
    list_initialize(&filter->workers);
    filter->dev.protocol_id = protocol_id;
    filter->ops = ops;
    filter_set_mode(filter, kFilterModeInitialized);
    return filter;
fail:
    if (filter) {
        free(filter);
    }
    return NULL;
}

filter_worker_t* filter_add_worker(filter_t* filter, filter_worker_f func, size_t num, bool is_default) {
    if (filter_get_mode(filter) != kFilterModeInitialized) {
        return NULL;
    }
    filter_worker_t* worker = filter_worker_start(func, num);
    if (!worker) {
        return NULL;
    }
    worker->filter = filter;
    list_add_tail(&filter->workers, &worker->node);
    if (is_default) {
        filter->default_worker = worker;
    }
    return worker;
}

mx_status_t filter_add(filter_t* filter, mx_device_t* parent) {
    if (filter_get_mode(filter) != kFilterModeInitialized) {
        return ERR_BAD_STATE;
    }
    mx_status_t rc = device_add(&filter->dev, parent);
    if (rc == NO_ERROR) {
        filter_set_mode(filter, kFilterModeRunning);
    }
    return rc;
}
