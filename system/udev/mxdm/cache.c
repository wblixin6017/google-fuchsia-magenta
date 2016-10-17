// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides a cache of recently accessed meta-data blocks.
//
// The cache is made up of a pool of preallocated block containers, a set of
// block buckets, and a least-recently-used (LRU) list.  When block data is
// requested from the cache, it checks the appropriate bucket based on the block
// number.  If it is found, it returns that block and moves it to the front of
// the LRU list, even if that block doesn't have data available yet.  If it's
// not found it takes the last block from the LRU list and repurposes it for the
// block in question.
//
// For cache misses, an I/O transaction will be generated to read the data;
// whether the data is available or not can be check with mxdm_block_is_ready.
// Similarly, cache evication of a block that has had data written to it will
// generate an I/O transaction to write that data back.

#define MXDM_IMPLEMENTATION

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ddk/iotxn.h>
#include <magenta/fuchsia-types.h>
#include <magenta/listnode.h>
#include <magenta/types.h>

#include "cache.h"
#include "common.h"
#include "mxdm.h"
#include "worker.h"

// Constants

// The number of buckets to use in the cache to speed lookup.
#define MXDM_NUM_BUCKETS 32
static_assert((MXDM_NUM_BUCKETS & (MXDM_NUM_BUCKETS - 1)) == 0,
              "MXDM_NUM_BUCKETS must be a power of two.");

// The total number of block cache entries.
#define MXDM_CACHE_SIZE 256

// Types

// The caching information for a block.
struct mxdm_block {
  // Handle used to link this block to the least-recently used queue.
  list_node_t lru_node;
  // Handle used to link this block to a cache bucket.
  list_node_t bucket_node;
  // I/O transactions waiting for this block to become ready.
  list_node_t dependencies;
  // An I/O transaction used to fetch/store data from/to the device.
  iotxn_t *txn;
  // Indicates if this block contains valid data from the device.
  uint32_t ready : 1;
  // Indicates if this block contains data to be written back to the device.
  uint32_t dirty : 1;
  // A count of how many times this block has been acquired and not released.
  uint32_t refcount : 30;
} __attribute__((__packed__));

// The collection of blocks making up the cache.
struct mxdm_cache {
  // A handle to the worker that created this cache
  mxdm_worker_t *worker;
  // A pool of pre-allocated blocks.
  mxdm_block_t blocks[MXDM_CACHE_SIZE];
  // Priority queue of the unreferenced blocks.  The tail block is the
  // least-recently used.
  list_node_t lru;
  // Active blocks, sorted into buckets for fast lookup.
  list_node_t buckets[MXDM_NUM_BUCKETS];
};

// Forward declarations

// Calculates the bucket number for a block at a given offset.
static uint32_t mxdm_cache_get_bucket(uint64_t blkoff);

// Public functions

bool mxdm_block_is_ready(const mxdm_block_t *block) {
  MXDM_IF_NULL(block, return false);
  return block->ready;
}

void mxdm_wait_for_block(mxdm_block_t *block, iotxn_t *txn) {
  MXDM_IF_NULL(block, return );
  MXDM_IF_NULL(txn, return );
  if (!block->ready && !list_in_list(&txn->node)) {
    list_add_tail(&block->dependencies, &txn->node);
  }
}

void mxdm_get_block(const mxdm_block_t *block, size_t offset, size_t length,
                    void *buffer) {
  MXDM_IF_NULL(block, return );
  MXDM_IF_NULL(buffer, return );
  block->txn->ops->copyfrom(block->txn, buffer, length, offset);
}

void mxdm_put_block(const void *buffer, size_t offset, size_t length,
                    mxdm_block_t *block) {
  MXDM_IF_NULL(block, return );
  MXDM_IF_NULL(buffer, return );
  block->txn->ops->copyto(block->txn, buffer, length, offset);
  block->dirty = 1;
}

// Protected functions

mx_status_t mxdm_cache_init(mxdm_worker_t *worker, mxdm_cache_t **out) {
  assert(worker);
  assert(out);
  mxdm_cache_t *cache = calloc(1, sizeof(mxdm_cache_t));
  if (!cache) {
    MXDM_TRACE("out of memory!");
    return ERR_NO_MEMORY;
  }
  cache->worker = worker;
  list_initialize(&cache->lru);
  for (size_t i = 0; i < MXDM_CACHE_SIZE; ++i) {
    mxdm_block_t *block = &cache->blocks[i];
    list_initialize(&block->dependencies);
    list_add_tail(&cache->lru, &block->lru_node);
  }
  for (size_t i = 0; i < MXDM_NUM_BUCKETS; ++i) {
    list_initialize(&cache->buckets[i]);
  }
  *out = cache;
  return NO_ERROR;
}

void mxdm_cache_free(mxdm_cache_t *cache) {
  if (cache) {
    free(cache);
  }
  return;
}

mx_status_t mxdm_cache_acquire(mxdm_cache_t *cache, uint64_t blkoff,
                               mxdm_block_t **out) {
  assert(cache);
  assert(out);
  mx_status_t rc = NO_ERROR;
  mxdm_block_t *block = NULL;
  // Look for a cached block in the buckets.
  uint32_t h = mxdm_cache_get_bucket(blkoff);
  list_node_t *bucket = &cache->buckets[h];
  list_for_every_entry(bucket, block, mxdm_block_t, bucket_node) {
    if (block->txn->offset == blkoff * MXDM_BLOCK_SIZE) {
      MXDM_TRACE("found block in cache for %lu", blkoff);
      goto found;
    }
  }
  // Not found, try to grab a block from LRU list.
  block = list_remove_tail_type(&cache->lru, mxdm_block_t, lru_node);
  if (!block) {
    MXDM_TRACE("out of cache; all blocks are busy");
    rc = ERR_NO_RESOURCES;
    goto fail;
  }
  // Make a new txn
  block->ready = false;
  if (block->txn) {
    block->txn->ops->release(block->txn);
  }
  rc = iotxn_alloc(&block->txn, 0, MXDM_BLOCK_SIZE, 0);
  if (rc < 0) {
    MXDM_TRACE("iotxn_alloc returned %d", rc);
    goto fail;
  }
  block->txn->opcode = IOTXN_OP_READ;
  block->txn->protocol = MX_PROTOCOL_BLOCK;
  block->txn->offset = blkoff * MXDM_BLOCK_SIZE;
  block->txn->length = MXDM_BLOCK_SIZE;
  rc = mxdm_worker_set_cb(cache->worker, block->txn, block);
  if (rc < 0) {
    goto fail;
  }
  // Put the block in the bucket.
  if (list_in_list(&block->bucket_node)) {
    list_delete(&block->bucket_node);
  }
  list_add_head(bucket, &block->bucket_node);
  // Queue the txn.
  mxdm_worker_queue(cache->worker, block->txn);
found:
  if (list_in_list(&block->lru_node)) {
    list_delete(&block->lru_node);
  }
  ++block->refcount;
  MXDM_TRACE("block %08x refcount incremented to %d", h, block->refcount);
  *out = block;
  return NO_ERROR;
fail:
  if (block && block->refcount == 0 && !list_in_list(&block->lru_node)) {
    list_add_tail(&cache->lru, &block->lru_node);
  }
  return rc;
}

void mxdm_cache_process(mxdm_block_t *block, iotxn_t *txn,
                        mxdm_worker_t *worker) {
  assert(block);
  assert(txn);
  assert(worker);
  if (txn->status == NO_ERROR && txn->actual == txn->length) {
    block->ready = true;
  }
  iotxn_t *tmp = NULL;
  iotxn_t *dep = NULL;
  list_for_every_entry_safe(&block->dependencies, dep, tmp, iotxn_t, node) {
    list_delete(&dep->node);
    mxdm_worker_queue(worker, dep);
  }
  if (block->dirty) {
    block->dirty = false;
    mxdm_release_block(worker, block);
  }
}

void mxdm_cache_release(mxdm_cache_t *cache, mxdm_block_t *block) {
  assert(cache);
  assert(block);
  --block->refcount;
  MXDM_TRACE("block %08x refcount decremented to %d",
             mxdm_cache_get_bucket(block->txn->offset / MXDM_BLOCK_SIZE),
             block->refcount);
  if (block->refcount != 0) {
    return;
  }
  if (!block->dirty) {
    block->txn->ops->release(block->txn);
    list_add_head(&cache->lru, &block->lru_node);
    return;
  }
  // Re-purpose the read txn to write back the data.
  ++block->refcount;
  MXDM_TRACE("block %08x refcount incremented to %d",
             mxdm_cache_get_bucket(block->txn->offset / MXDM_BLOCK_SIZE),
             block->refcount);
  block->ready = false;
  iotxn_t *block_txn = block->txn;
  block_txn->opcode = IOTXN_OP_WRITE;
  block_txn->actual = 0;
  block_txn->status = NO_ERROR;
  // Queue the txn.
  // TODO(aarongreen): What if we don't queue immediately, delay until something
  // else is dirtied/this block is being evicted/lru is running low?
  mxdm_worker_queue(cache->worker, block_txn);
}

// Private functions

static uint32_t mxdm_cache_get_bucket(uint64_t blkoff) {
  // Perform djb2a on the offset.
  uint32_t h = 5381;
  for (size_t i = 0; i < sizeof(uint64_t); ++i) {
    h = ((h << 5) + h) ^ ((blkoff >> i) & 0xFF);
  }
  return h & (MXDM_NUM_BUCKETS - 1);
}

#undef MXDM_IMPLEMENTATION
