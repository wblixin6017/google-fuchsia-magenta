// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <ddk/iotxn.h>

////////////////
// MXDM driver
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
// to remain responsive to devmgr.  In the code below, functions (including
// callbacks) that have a "mxdm_worker_t" parameter are executed on the worker
// thread. When implementing the callbacks, only those "worker functions" should
// be used.
//
// Also in the code below, location and size argument and variable names are
// chosen to clearly indicate the quantity being referred to:
//   * bitoff, bitlen, etc.: An offset, length, etc. in bits, e.g. within a
//   bitmap.
//   * offset, length, etc.: An offset, length, etc. in bytes, e.g. within a
//   buffer.
//   * blkoff, blklen, etc.: An offset, length, etc. in blocks, e.g. within a
//   block device.

////////////////
// Constants

// MXDM_BLOCK_SIZE is the size of a block of data.  The actual block device's
// block size must divide this number evenly.
#define MXDM_BLOCK_SIZE 8192

////////////////
// Types

// Driver control structure representing the overall driver state.
typedef struct mxdm mxdm_t;

// Worker thread control structure representing the I/O transaction processor.
typedef struct mxdm_worker mxdm_worker_t;

// Cache control structure representing a block of data from the device.
typedef struct mxdm_block mxdm_block_t;

// Returned by the I/O transaction callbacks, this indicates what the MXDM
// framework should do next with the I/O transaction.
typedef enum mxdm_txn_action {
  kMxdmIgnoreTxn,
  kMxdmRequeueTxn,
  kMxdmContinueTxn,
  kMxdmCompleteTxn,
} mxdm_txn_action_t;

// Callbacks to the specific MXDM driver implementation.
typedef struct mxdm_ops {
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
  // Handles an IOCTL.  The implementing driver can pass a particular 'op' to
  // the framework and/or parent device by returning ERR_NOT_SUPPORTED.
  ssize_t (*ioctl)(mxdm_t* mxdm, uint32_t op, const void* in_buf, size_t in_len,
                   void* out_buf, size_t out_len);
  // Called by the worker before sending an IOTXN_OP_READ transaction to the
  // parent device.
  mxdm_txn_action_t (*before_read)(mxdm_worker_t* worker, iotxn_t* txn,
                                   uint64_t* blkoff, uint64_t blkmax);
  // Called by the worker before sending an IOTXN_OP_WRITE transaction to the
  // parent device.
  mxdm_txn_action_t (*before_write)(mxdm_worker_t* worker, iotxn_t* txn,
                                    uint64_t* blkoff, uint64_t blkmax);
  // Called by the worker afetr an IOTXN_OP_READ transaction is completed by
  // the parent device.
  mxdm_txn_action_t (*after_read)(mxdm_worker_t* worker, iotxn_t* txn,
                                  uint64_t* blkoff, uint64_t blkmax);
  // Called by the worker afetr an IOTXN_OP_WRITE transaction is completed by
  // the parent device.
  mxdm_txn_action_t (*after_write)(mxdm_worker_t* worker, iotxn_t* txn,
                                   uint64_t* blkoff, uint64_t blkmax);
} mxdm_ops_t;

////////////////
// Functions

////////
// Constructor/destructor functions

// Creates an MXDM block device filter driver.  It allocates the necessary
// resources and starts the worker thread.  The goal of this function is to be
// fast, any expensive initialization (including the prepare callback) is done
// by the worker thread in mxdm_worker_init.  The context_size argument reserves
// memory that can be retrieved by mxdm_get_context and used as a specific
// structure by an implementing driver.
mx_status_t mxdm_init(mx_driver_t* drv, mx_device_t* parent, const char* suffix,
                      const mxdm_ops_t* ops, size_t context_size);

////////
// Helper functions

// Gets the mxdm that owns the given worker.
mxdm_t* mxdm_from_worker(mxdm_worker_t* worker);

// Returns a pointer to the memory reserved for the implementing driver.  This
// function doesn't take a worker argument, and may be called from more than one
// thread.  It is the caller's responsibility to synchronize access to the
// memory pointed at by the returned pointer.
void* mxdm_get_context(mxdm_t* mxdm);

////////
// Block I/O

// Synchronously reads 'length' bytes from the block given by 'blkoff' on 'dev',
// and puts the data in 'out'.
mx_status_t mxdm_read(mxdm_t* mxdm, uint64_t blkoff, void* out, size_t length);
// Synchronously writes 'length' bytes from 'buffer'
mx_status_t mxdm_write(mxdm_t* mxdm, uint64_t blkoff, const void* buffer,
                       size_t length);

////////
// Block caching functions

// Attempts to find the block given by 'blkoff' in the block cache or insert a
// block if it isn't found.  The block is returned in 'out', but may not be
// ready (e.g. the block has an incomplete I/O request).  The block is
// effectively pinned, and can't be reused until it is released.
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

// Returns true if the block offset falls in the "data" region of the block
// device.
bool mxdm_is_data(mxdm_worker_t* worker, uint64_t blkoff);
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
