// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/iotxn.h>
#include <magenta/types.h>

// This file defines the public interface of the MXDM block device filter
// driver framework.
//
// This code provides a framework for making block device filter drivers.  It
// allows drivers to register callbacks on device creation and removal, ioctls,
// and before and after reading or writing data to the device.
//
// Additionally, it provides facilities to cache "metadata" blocks associated
// with data blocks, and to quickly check or mark blocks.
//
// The MXDM driver framework provides a "worker" thread under the covers to
// handle any non-trivial work associated with requests.  This allows the device
// to remain responsive to devmgr.  The callbacks in mxdm_worker_ops_t are
// invoked from the worker thread.
//
// Also in the code below, location and size argument and variable names are
// chosen to clearly indicate the quantity being referred to:
//   * bitoff, bitlen, etc.: An offset, length, etc. in bits, e.g. within a
//     bitmap.
//   * offset, length, etc.: An offset, length, etc. in bytes, e.g. within a
//     buffer.
//   * blkoff, blklen, etc.: An offset, length, etc. in blocks, e.g. within a
//     block device.

#include <magenta/device/mxdm.h>

// Types

// MXDM device object.
typedef struct mxdm_device mxdm_device_t;

// Cache control structure representing a block of data from the device.
typedef struct mxdm_block mxdm_block_t;

// Worker thread control structure representing the I/O transaction processor.
typedef struct mxdm_worker mxdm_worker_t;

// Returned by the I/O transaction callbacks, this indicates what the MXDM
// framework should do next with the I/O transaction.
typedef enum mxdm_txn_action {
  kMxdmIgnoreTxn,
  kMxdmRequeueTxn,
  kMxdmContinueTxn,
  kMxdmCompleteTxn,
} mxdm_txn_action_t;

// Callbacks to the specific MXDM driver implementation from any thread.
typedef struct mxdm_device_ops {
  // Handles an IOCTL.  The implementing driver can pass a particular 'op' to
  // the framework and/or parent device by returning ERR_NOT_SUPPORTED.
  ssize_t (*ioctl)(mxdm_device_t* device, uint32_t op, const void* in_buf,
                   size_t in_len, void* out_buf, size_t out_len);
} mxdm_device_ops_t;

// Callbacks to the specific MXDM driver implementation from the worker thread.
typedef struct mxdm_worker_ops {
  // Called by the worker thread as it starts, this callback allows the
  // implementing driver to do any needed asynchronous initialization.  The
  // driver should fill in data_blkoff and data_blklen with the offset and
  // length of the data blocks.
  mx_status_t (*prepare)(mxdm_worker_t* worker, uint64_t blklen,
                         uint64_t* data_blkoff, uint64_t* data_blklen);
  // Called by the now-detached worker just before it deletes the MXDM control
  // structure.  The implementing driver must free any resources it holds
  // except the MXDM control structure.
  mx_status_t (*release)(mxdm_worker_t* worker);
  // Called by the worker before sending an IOTXN_OP_READ transaction to the
  // parent device.  This callback is optional.
  mxdm_txn_action_t (*before_read)(mxdm_worker_t* worker, iotxn_t* txn,
                                   uint64_t* blkoff, uint64_t blkmax);
  // Called by the worker before sending an IOTXN_OP_WRITE transaction to the
  // parent device.  This callback is optional.
  mxdm_txn_action_t (*before_write)(mxdm_worker_t* worker, iotxn_t* txn,
                                    uint64_t* blkoff, uint64_t blkmax);
  // Called by the worker after an IOTXN_OP_READ transaction is completed by
  // the parent device.  This callback is optional.
  mxdm_txn_action_t (*after_read)(mxdm_worker_t* worker, iotxn_t* txn,
                                  uint64_t* blkoff, uint64_t blkmax);
  // Called by the worker after an IOTXN_OP_WRITE transaction is completed by
  // the parent device.  This callback is optional.
  mxdm_txn_action_t (*after_write)(mxdm_worker_t* worker, iotxn_t* txn,
                                   uint64_t* blkoff, uint64_t blkmax);
} mxdm_worker_ops_t;

// Functions

// Constructor function

// Creates an MXDM block device filter driver.  It allocates the necessary
// resources and starts the worker thread.
//
// This function is meant to be fast, any expensive initialization (including
// the prepare callback) is done by the worker thread in mxdm_worker_init.
//
// Only the ioctl callback is used  from dev_ops. The other device callbacks are
// ignored.
//
// The context_size argument reserves memory that can be retrieved by
// mxdm_get_context and used as a specific structure by an implementing driver.
mx_status_t mxdm_init(mx_driver_t* drv, mx_device_t* parent, const char* suffix,
                      const mxdm_device_ops_t* dev_ops,
                      const mxdm_worker_ops_t* worker_ops, size_t context_size);

// Helper functions

// Returns the MXDM device associated with the given worker.
const mxdm_device_t* mxdm_worker_get_device(mxdm_worker_t* worker);

// Returns a pointer to the memory reserved for the implementing driver.  This
// memory may be accessed by multiple threads concurrently, it is the caller's
// responsibility to synchronize access.
void* mxdm_worker_get_context(mxdm_worker_t* worker);

// Returns a pointer to the memory reserved for the implementing driver.  This
// memory may be accessed by multiple threads concurrently, it is the caller's
// responsibility to synchronize access.
void* mxdm_device_get_context(mxdm_device_t* device);

// Returns true if the block offset falls in the "data" region of the block
// device.
bool mxdm_is_data_block(const mxdm_worker_t* worker, uint64_t blkoff);

// Block I/O

// Synchronously reads 'length' bytes from the block given by 'blkoff' on 'dev',
// and puts the data in 'out'.  Since this function blocks, it should not be
// used in a before/after_read/write callback.
mx_status_t mxdm_read_block(const mxdm_device_t* device, uint64_t blkoff,
                            void* out, size_t length);

// Synchronously writes 'length' bytes from 'buffer' to the block given by
// 'blkoff' on 'device'.  Since this function blocks, it should not be used in a
// before/after_read/write callback.
mx_status_t mxdm_write_block(const mxdm_device_t* device, uint64_t blkoff,
                             const void* buffer, size_t length);

// Block caching functions

// Attempts to find the block given by 'blkoff' in the block cache or insert a
// block if it isn't found.  The block is returned in 'out', but may not be
// ready (e.g. the block has an incomplete I/O request).
mx_status_t mxdm_acquire_block(mxdm_worker_t* worker, uint64_t blkoff,
                               mxdm_block_t** out);

// Returns true if the block has finished it's I/O and had valid data.
bool mxdm_block_is_ready(const mxdm_block_t* block);

// If the block is not ready, adds the txn to the block's list of dependent
// txns.  When the block becomes ready, it will put the txns back on the worker
// queue.
void mxdm_wait_for_block(mxdm_block_t* block, iotxn_t* txn);

// Gets data from 'block' at the given 'offset' and 'length' and copies it to
// the 'buffer'.
void mxdm_get_block(const mxdm_block_t* block, size_t offset, size_t length,
                    void* buffer);

// Puts data from the 'buffer' into the 'block' at the given 'offset' and
// 'length'.
void mxdm_put_block(const void* buffer, size_t offset, size_t length,
                    mxdm_block_t* block);

// Unpins a block, allowing it to be reused.
void mxdm_release_block(mxdm_worker_t* worker, mxdm_block_t* block);

////////
// Block-marking functions

// Returns true if the given block offset is currently marked.
bool mxdm_check_block(mxdm_worker_t* worker, uint64_t blkoff);

// Returns true if the given range of block offsets are currently marked.
// Otherwise, it sets 'blkoff' to the first block that is not marked and returns
// false.
bool mxdm_check_blocks(mxdm_worker_t* worker, uint64_t* blkoff,
                       uint64_t blkmax);

// Marks a block given by 'blkoff'.  This may fail if the underlying data
// structure needs to decompress to fulfill the request but fails to do so, e.g.
// if OOM.
mx_status_t mxdm_mark_block(mxdm_worker_t* worker, uint64_t blkoff);

// Resets the given range of block offsets to be unmarked.  This may fail if the
// underlying data structure needs to decompress to fulfill the request but
// fails to do so, e.g. if OOM.
mx_status_t mxdm_clear_blocks(mxdm_worker_t* worker, uint64_t blkoff,
                              uint64_t blkmax);
