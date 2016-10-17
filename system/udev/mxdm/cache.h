// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines the protected cache functions available to the worker 
// thread.  This header should NOT be included by drivers; see the comments in
// common.h for additional info..

#pragma once

#ifndef MXDM_IMPLEMENTATION
#error "This file should only be included by the MXDM framework."
#endif

#include <stdint.h>

#include <ddk/iotxn.h>
#include <magenta/types.h>

#include "mxdm.h"

// Types

// A cache of recently accessed metadata blocks.
typedef struct mxdm_cache mxdm_cache_t;

// Functions

// Initializes the block cache for used by the given worker.
mx_status_t mxdm_cache_init(mxdm_worker_t* worker, mxdm_cache_t** out);

// Frees any resources associated with the given cache.
void mxdm_cache_free(mxdm_cache_t* cache);

// Attempts to find the block given by 'blkoff' in the block cache or insert a
// block if it isn't found.  The block is returned in 'out', but may not be
// ready (e.g. the block has an incomplete I/O request).  The block is
// effectively pinned, and can't be reused until it is released.
mx_status_t mxdm_cache_acquire(mxdm_cache_t* cache, uint64_t blkoff,
                               mxdm_block_t** out);

// Called when an I/O transaction for a metadata block completes.  It marks the
// block as ready and re-queues any I/O transactions that were waiting on it.
void mxdm_cache_process(mxdm_block_t* block, iotxn_t* txn,
                        mxdm_worker_t* worker);

// Unpins a block, allowing it to be reused.  If the block is dirty, this may
// queue and I/O transaction to write it back.
void mxdm_cache_release(mxdm_cache_t* cache, mxdm_block_t* block);
