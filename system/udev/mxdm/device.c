// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides the device callback implementation to register a device
// with devmgr and to pass I/O controls and transactions to the underlying block
// device.  All external interactions come through this code, but it hands off
// any non-trivial work to the worker thread.

#define MXDM_IMPLEMENTATION

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <threads.h>

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/iotxn.h>
#include <magenta/device/block.h>
#include <magenta/fuchsia-types.h>
#include <magenta/types.h>

#include "common.h"
#include "device.h"
#include "mxdm.h"

// Types

// The MXDM device object, which associates the underlying block device, the
// worker thread, and the implementing driver's device callbacks and driver
// specific memory.
struct mxdm_device {
  // This device's node in devmgr's device tree.
  mx_device_t dev;
  // Driver specific callbacks.
  mxdm_device_ops_t ops;
  // Worker thread control structure.
  mxdm_worker_t* worker;
  // Size of the context object, specified in mxdm_init.
  size_t context_size;
  // Variable length context object.
  uint8_t context[0];
};

// Forward declarations

// Retrieves the MXDM device object pointer from a devmgr device handle.
static mxdm_device_t* mxdm_device_get(mx_device_t* dev);

// Creates a synchronous I/O transaction for use in mxdm_sync_io.
static mx_status_t mxdm_device_sync_init(const mxdm_device_t* device,
                                         uint64_t blkoff, size_t length,
                                         iotxn_t** out);

// Queues a synchronous I/O transaction created by mxdm_sync_init and waits for
// it to complete.
static mx_status_t mxdm_device_sync_io(const mxdm_device_t* device,
                                       iotxn_t* txn);

// Called when txn completes, this function signals the waiting caller to
// 'mxdm_sync_io'.
static void mxdm_device_sync_cb(iotxn_t* txn, void* cookie);

// Process an I/O control.  It first calls the ioctl callback.  If that returns
// ERR_NOT_SUPPORTED, it tries to handle it locally.  If is does not recognize
// 'op' it finally passes it to the parent device.
static ssize_t mxdm_device_ioctl(mx_device_t* dev, uint32_t op,
                                 const void* in_buf, size_t in_len,
                                 void* out_buf, size_t out_len);

// Always returns ERR_NOT_SUPPORTED.  This callback is used if none was is given
// in mxdm_init.
static ssize_t mxdm_device_ioctl_default(mxdm_device_t* device, uint32_t op,
                                         const void* in_buf, size_t in_len,
                                         void* out_buf, size_t out_len);

// Can be called via I/O ctl or directly, this returns the usable size of the
// device (i.e. the aggregate size of the "data" blocks).
static mx_off_t mxdm_device_get_size(mx_device_t* dev);

// Accepts an external I/O transaction.  The iotxn is not immediately sent to
// the parent, but instead is put on the worker's queue for processing via
// mxdm_worker_queue.
static void mxdm_device_iotxn_queue(mx_device_t* dev, iotxn_t* txn);

// Notifies the worker that the device is going away.  All outstanding I/O
// transactions will eventually fail with ERR_HANDLE_CLOSED.
static void mxdm_device_unbind(mx_device_t* dev);

// Notifies the worker that is safe to begin tear-down and clean-up.  This
// function also detaches the worker.  The worker will asynchronously complete
// the clean-up (including the release callback) and then exit.
static mx_status_t mxdm_device_release(mx_device_t* dev);

// Constants

static mx_protocol_device_t mxdm_proto = {
    .unbind = mxdm_device_unbind,
    .release = mxdm_device_release,
    .iotxn_queue = mxdm_device_iotxn_queue,
    .get_size = mxdm_device_get_size,
    .ioctl = mxdm_device_ioctl,
};

// Public functions

mx_status_t mxdm_init(mx_driver_t* drv, mx_device_t* parent, const char* suffix,
                      const mxdm_device_ops_t* device_ops,
                      const mxdm_worker_ops_t* worker_ops,
                      size_t context_size) {
  MXDM_TRACE_INIT();
  MXDM_IF_NULL(drv, return ERR_INVALID_ARGS);
  MXDM_IF_NULL(parent, return ERR_INVALID_ARGS);
  MXDM_IF_NULL(suffix, return ERR_INVALID_ARGS);
  MXDM_IF_NULL(worker_ops, return ERR_INVALID_ARGS);
  MXDM_IF_NULL(worker_ops->prepare, return ERR_INVALID_ARGS);
  MXDM_IF_NULL(worker_ops->release, return ERR_INVALID_ARGS);
  mx_status_t rc = NO_ERROR;
  mxdm_device_t* device = NULL;
  mxdm_init_info_t* info = NULL;
  // Create the MXDM device.
  device = calloc(1, sizeof(mxdm_device_t) + context_size);
  info = calloc(1, sizeof(mxdm_init_info_t));
  if (!device || !info) {
    MXDM_TRACE("out of memory!");
    return ERR_NO_MEMORY;
  }
  device->ops.ioctl =
      (device_ops && device_ops->ioctl ? device_ops->ioctl
                                       : mxdm_device_ioctl_default);
  device->context_size = context_size;
  // Fill in the initialization info.
  info->drv = drv;
  info->parent = parent;
  info->ops = worker_ops;
  snprintf(info->name, sizeof(info->name), "%s-%s", parent->name, suffix);
  info->device = device;
  // Create a detached thread that will cleanup after itself.  The thread
  // takes ownership of device and info.
  thrd_t thrd;
  if (thrd_create(&thrd, mxdm_worker, info) != thrd_success) {
    MXDM_TRACE("thrd_create failed");
    mxdm_device_free(device);
    free(info);
    return ERR_NO_RESOURCES;
  }
  // thrd_detach should only fail if the worker has already exited.
  if (thrd_detach(thrd) != thrd_success) {
    MXDM_TRACE("thrd_detach failed");
    thrd_join(thrd, &rc);
    return rc;
  }
  return NO_ERROR;
}

void* mxdm_device_get_context(mxdm_device_t* device) {
  MXDM_IF_NULL(device, return NULL);
  return (void*)device->context;
}

mx_status_t mxdm_read_block(const mxdm_device_t* device, uint64_t blkoff,
                            void* out, size_t length) {
  MXDM_IF_NULL(device, return ERR_INVALID_ARGS);
  MXDM_IF_NULL(out, return ERR_INVALID_ARGS);
  iotxn_t* txn = NULL;
  mx_status_t rc = mxdm_device_sync_init(device, blkoff, length, &txn);
  if (rc < 0) {
    return rc;
  }
  txn->opcode = IOTXN_OP_READ;
  rc = mxdm_device_sync_io(device, txn);
  if (rc < 0) {
    return rc;
  }
  txn->ops->copyfrom(txn, out, 0, length);
  return NO_ERROR;
}

mx_status_t mxdm_write_block(const mxdm_device_t* device, uint64_t blkoff,
                             const void* buffer, size_t length) {
  MXDM_IF_NULL(device, return ERR_INVALID_ARGS);
  MXDM_IF_NULL(buffer, return ERR_INVALID_ARGS);
  iotxn_t* txn = NULL;
  mx_status_t rc = mxdm_device_sync_init(device, blkoff, length, &txn);
  if (rc < 0) {
    return rc;
  }
  txn->opcode = IOTXN_OP_WRITE;
  txn->ops->copyto(txn, buffer, 0, length);
  rc = mxdm_device_sync_io(device, txn);
  if (rc < 0) {
    return rc;
  }
  return NO_ERROR;
}

// Protected functions

mx_status_t mxdm_device_init(mxdm_worker_t* worker, mxdm_init_info_t* info) {
  assert(worker);
  assert(info);
  mxdm_device_t* device = info->device;
  device->worker = worker;
  device_init(&device->dev, info->drv, info->name, &mxdm_proto);
  device->dev.protocol_id = MX_PROTOCOL_BLOCK;
  mx_status_t rc = device_add(&device->dev, info->parent);
  if (rc < 0) {
    MXDM_TRACE("device_add returned %d", rc);
  }
  return rc;
}

void mxdm_device_free(mxdm_device_t* device) {
  if (device) {
    free(device);
  }
}

void mxdm_device_queue(mxdm_device_t* device, iotxn_t* txn) {
  assert(device);
  assert(txn);
  mx_device_t* parent = device->dev.parent;
  parent->ops->iotxn_queue(parent, txn);
}

// Private functions
// We use MXDM_IF_NULL instead of assert for functions that are actually
// callbacks from devmgr, since they're an external interface despite being
// static.

static mxdm_device_t* mxdm_device_get(mx_device_t* dev) {
  assert(dev);
  return containerof(dev, mxdm_device_t, dev);
}

static mx_status_t mxdm_device_sync_init(const mxdm_device_t* device,
                                         uint64_t blkoff, size_t length,
                                         iotxn_t** out_txn) {
  assert(device);
  assert(out_txn);
  mx_device_t* parent = device->dev.parent;
  if (blkoff * MXDM_BLOCK_SIZE >= parent->ops->get_size(parent)) {
    MXDM_TRACE("invalid offet: %lu", length);
    return ERR_INVALID_ARGS;
  }
  if (length == 0) {
    MXDM_TRACE("too short: %zu", length);
    return ERR_INVALID_ARGS;
  }
  if (length > MXDM_BLOCK_SIZE) {
    MXDM_TRACE("too long: %zu", length);
    return ERR_INVALID_ARGS;
  }
  iotxn_t* txn = NULL;
  mx_status_t rc = iotxn_alloc(&txn, 0, MXDM_BLOCK_SIZE, 0);
  if (rc < 0) {
    MXDM_TRACE("iotxn_alloc returned %d", rc);
    return rc;
  }
  txn->protocol = MX_PROTOCOL_BLOCK;
  txn->offset = blkoff * MXDM_BLOCK_SIZE;
  txn->length = MXDM_BLOCK_SIZE;
  *out_txn = txn;
  return NO_ERROR;
}

static mx_status_t mxdm_device_sync_io(const mxdm_device_t* device,
                                       iotxn_t* txn) {
  assert(device);
  assert(txn);
  mx_device_t* parent = device->dev.parent;
  completion_t completion = COMPLETION_INIT;
  txn->complete_cb = mxdm_device_sync_cb;
  txn->cookie = &completion;
  iotxn_queue(parent, txn);
  completion_wait(&completion, MX_TIME_INFINITE);
  if (txn->actual < txn->length) {
    MXDM_TRACE("incomplete I/O: only %lu of %lu", txn->actual, txn->length);
    return ERR_IO;
  }
  return txn->status;
}

static void mxdm_device_sync_cb(iotxn_t* txn, void* cookie) {
  assert(txn);
  assert(cookie);
  completion_signal((completion_t*)cookie);
}

static ssize_t mxdm_device_ioctl(mx_device_t* dev, uint32_t op,
                                 const void* in_buf, size_t in_len,
                                 void* out_buf, size_t out_len) {
  MXDM_IF_NULL(dev, return ERR_INVALID_ARGS);
  mxdm_device_t* device = mxdm_device_get(dev);
  ssize_t rc = device->ops.ioctl(device, op, in_buf, in_len, out_buf, out_len);
  if (rc != ERR_NOT_SUPPORTED) {
    return rc;
  }
  switch (op) {
    case IOCTL_BLOCK_GET_SIZE: {
      uint64_t* size = out_buf;
      if (!size || out_len < sizeof(*size)) {
        return ERR_BUFFER_TOO_SMALL;
      }
      *size = mxdm_device_get_size(dev);
      return sizeof(*size);
    }
    case IOCTL_BLOCK_GET_BLOCKSIZE: {
      uint64_t* blksize = out_buf;
      if (!blksize || out_len < sizeof(*blksize)) {
        return ERR_BUFFER_TOO_SMALL;
      }
      *blksize = MXDM_BLOCK_SIZE;
      return sizeof(*blksize);
    }
    default: {
      mx_device_t* parent = dev->parent;
      return parent->ops->ioctl(parent, op, in_buf, in_len, out_buf, out_len);
    }
  }
}

static ssize_t mxdm_device_ioctl_default(mxdm_device_t* device, uint32_t op,
                                         const void* in_buf, size_t in_len,
                                         void* out_buf, size_t out_len) {
  assert(device);
  return ERR_NOT_SUPPORTED;
}

static mx_off_t mxdm_device_get_size(mx_device_t* dev) {
  MXDM_IF_NULL(dev, return 0);
  mxdm_device_t* device = mxdm_device_get(dev);
  return mxdm_worker_data_size(device->worker);
}

static void mxdm_device_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
  MXDM_IF_NULL(dev, return );
  MXDM_IF_NULL(txn, return );
  if (txn->length == 0) {
    txn->ops->complete(txn, ERR_INVALID_ARGS, 0);
    return;
  }
  mxdm_device_t* device = mxdm_device_get(dev);
  txn->context = device;
  mxdm_worker_queue(device->worker, txn);
}

static void mxdm_device_unbind(mx_device_t* dev) {
  MXDM_IF_NULL(dev, return );
  mxdm_device_t* device = mxdm_device_get(dev);
  mxdm_worker_stop(device->worker);
}

static mx_status_t mxdm_device_release(mx_device_t* dev) {
  MXDM_IF_NULL(dev, return ERR_INVALID_ARGS);
  mx_device_t* child = NULL;
  mx_device_t* temp = NULL;
  list_for_every_entry_safe(&dev->children, child, temp, mx_device_t, node) {
    device_remove(child);
  }
  mxdm_device_t* device = mxdm_device_get(dev);
  mxdm_worker_exit(device->worker);
  return NO_ERROR;
}

#undef MXDM_IMPLEMENTATION
