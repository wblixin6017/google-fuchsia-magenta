// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <threads.h>

#include <ddk/common/mxdm.h>
#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/ioctl.h>
#include <ddk/iotxn.h>
#include <magenta/device/block.h>
#include <magenta/fuchsia-types.h>
#include <magenta/listnode.h>

// TODO(aarongreen): More words about the worker thread.
//
// There are three types of I/O transactions external as far as MXDM is
// concerned:
// 1. External transactions come in through iotxn_queue, which sets the context
//    field to inform the worker that it needs to clone the transaction.
// 2. Cloned transactions are created from external transactions in
//    mxdm_clone_txn. Cloning allow the worker to modify fields before passing
//    the transaction along. Clones are distinguished by having a NULL context
//    and an offset in the device's data region.
// 3. Cache transactions are created by metadata cache misses in
//    mxdm_acquire_block.  They are distinguished by having a NULL context and
//    an offset outside the device's data region.

////////////////
// Preprocessor definitions

// TODO(aarongreen): These are just initial guesses.  I need to define some
// performance metrics, and then experiment and measure with different values.

// The number of bits in a bitmap chunk.
#define MXDM_BITS_PER_CHUNK (MXDM_BLOCK_SIZE * 8)

// The number of buckets to use in the cache to speed lookup.
#define MXDM_NUM_BUCKETS 32
static_assert((MXDM_NUM_BUCKETS & (MXDM_NUM_BUCKETS - 1)) == 0,
              "MXDM_NUM_BUCKETS must be a power of two.");

// The total number of block cache entries.
#define MXDM_CACHE_SIZE 256

////////////////
// Macros

#ifdef TRACE
static mtx_t mxdm_trace_mtx;
// Allows multi-threaded tracing
#define MXDM_TRACE_INIT()                                     \
  if (mtx_init(&mxdm_trace_mtx, mtx_plain) != thrd_success) { \
    assert(0);                                                \
  }
// If enabled, prints a message.
#define MXDM_TRACE(fmt...)                  \
  mtx_lock(&mxdm_trace_mtx);                \
  printf("%16s:%-4d ", __FILE__, __LINE__); \
  printf(fmt);                              \
  printf("\n");                             \
  mtx_unlock(&mxdm_trace_mtx);
#else
#define MXDM_TRACE_INIT() ;
#define MXDM_TRACE(fmt...) ;
#endif  // TRACE

#define MXDM_CHECK_NULL(arg)          \
  if (!arg) {                         \
    MXDM_TRACE("'%s' is NULL", #arg); \
    return;                           \
  }

#define MXDM_CHECK_NULL_RET(arg, retval) \
  if (!arg) {                            \
    MXDM_TRACE("'%s' is NULL", #arg);    \
    return retval;                       \
  }

////////////////
// Types

////////
// I/O transaction protocol data types

// I/O transaction information that is passed to its completion callback.
typedef struct mxdm_txn_cookie {
  // Handle to the worker control structure.
  mxdm_worker_t *worker;
  // Represents the origin of this internal I/O transaction, either a
  // cache-miss on a block or an external I/O transaction.
  union {
    mxdm_block_t *block;
    iotxn_t *txn;
  } origin;
  // The starting block (inclusive) of the I/O transaction.  This value is
  // updated by the I/O callbacks to track progress when an I/O transaction
  // requires more processing than can be done at once.
  uint64_t blkoff;
  // Ending block (exclusive) of the I/O transaction.
  uint64_t blkmax;
} mxdm_txn_cookie_t;

////////
// Bitmap data types

// A run-length encoding (RLE) of a sequence of bits set to 1.
typedef struct mxdm_bitmap_rle_elem {
  // Handle used to link this element to a RLE chunk.
  list_node_t node;
  // The start of this run of 1-bits.
  uint64_t bitoff;
  // The number of 1-bits in this run.
  uint64_t bitlen;
} mxdm_bitmap_rle_elem_t;

// A compressed chunk of a bitmap made up of a list of RLEs.
typedef struct mxdm_bitmap_rle {
  // The number of elements in 'elems'.
  size_t length;
  // A list of mxdm_bitmap_rle_elem_t.
  list_node_t elems;
} mxdm_bitmap_rle_t;

// A bitmap made of compressible chunks
typedef struct mxdm_bitmap {
  // The numbers of bits in this map.
  uint64_t bitlen;
  // The numbers of chunks of bits in this map, which is the length of 'data'.
  uint64_t chunks;
  // An array of chunks of bitmap data.  Each is either an uncompressed array of
  // bits or a run-length encoding list.
  union {
    uint64_t *raw;
    mxdm_bitmap_rle_t *rle;
  } * data;
  // A smaller bitmap indicating which chunks are RLE chunks.  This field is
  // NULL when 'bitlen' is less than MXDM_BITS_PER_CHUNK, and the sole chunk
  // in 'data' is always raw.
  struct mxdm_bitmap *use_rle;
} mxdm_bitmap_t;

////////
// Block cache data types

// The caching information for a block.
struct mxdm_block {
  // An I/O transaction used to fetch/store data from/to the device.
  iotxn_t *txn;
  // Indicates if this block contains valid data from the device.
  uint32_t ready : 1;
  // Indicates if this block contains data to be written back to the device.
  uint32_t dirty : 1;
  // A count of how many times this block has been acquired and not released.
  uint32_t refcount : 30;
  // Handle used to link this block to the least-recently used queue.
  list_node_t lru_node;
  // Handle used to link this block to a cache bucket.
  list_node_t bucket_node;
  // I/O transactions waiting for this block to become ready.
  list_node_t dependencies;
} __attribute__((__packed__));

////////
// Worker data types

// Represents the state of a worker.
typedef enum mxdm_worker_state {
  kWorkerWorking,
  kWorkerStopping,
  kWorkerExiting,
} mxdm_worker_state_t;

// Control structure for the worker thread.
struct mxdm_worker {
  // The thread ID for the worker thread.
  thrd_t thrd;
  // Mutex used to synchronize the worker's state and queue.
  mtx_t mtx;
  // Condition variable used to signal the worker.
  cnd_t cnd;
  // The current state of the worker thread.
  mxdm_worker_state_t state;
  // A list of external I/O transactions waiting for the worker to complete
  // them.
  list_node_t txns;
  // A queue of I/O transactions that the worker will process.
  list_node_t queue;
  // A pool of pre-allocated blocks.
  mxdm_block_t cache[MXDM_CACHE_SIZE];
  // Active blocks, sorted into buckets for fast lookup.
  list_node_t buckets[MXDM_NUM_BUCKETS];
  // Priority queue of the unreferenced blocks.  The tail block is the
  // least-recently used.
  list_node_t lru;
  // A bitmap over the blocks of the device.
  mxdm_bitmap_t *bitmap;
};

////////
// Driver types

// Control structure for the MXDM driver.
struct mxdm {
  // This device's node in devmgr's device tree.
  mx_device_t dev;
  // Driver specific callbacks.
  mxdm_ops_t ops;
  // Worker thread control structure.
  mxdm_worker_t worker;
  // Offset of the first data block.
  uint64_t data_blkoff;
  // Number of data blocks.
  uint64_t data_blklen;
  // Size of the context object, specified in mxdm_init.
  size_t context_size;
  // Variable length context object.
  uint8_t context[0];
};

// Initialization info passed to the worker thread.
typedef struct mxdm_init_info {
  // The MXDM control structure.
  mxdm_t *mxdm;
  // The specific MXDM driver binding to the device.
  mx_driver_t *drv;
  // "Parent" device in devmgr's device tree.
  mx_device_t *dev;
  // Name of the device
  char name[MX_DEVICE_NAME_MAX];
} mxdm_init_info_t;

////////////////
// Forward declarations
// See the comments in mxdm.h regarding naming.

////////
// Constructor/destructor functions

// Frees any memory and/or resources associated with 'mxdm'.  This should only
// be called by the worker during clean-up, or by mxdm_init on a pre-worker
// fatal error.
static mx_status_t mxdm_free(mxdm_t *mxdm);

////////
// Helper functions

// Gets the mxdm that owns the given device.
static mxdm_t *mxdm_from_device(mx_device_t *dev);

////////
// Worker functions

// This is the mxdm worker thread.  It is started by mxdm_init and should not
// return until the device is released. 'arg' is an mxdm_init_info structure.
static int mxdm_worker(void *arg);
// Performs the asynchronous portion of the device setup.  Anything that might
// cause mxdm_init to take more than a trivial amount of time is moved to this
// function on the worker thread.
static mx_status_t mxdm_worker_init(mxdm_init_info_t *info);
// Processes iotxns from the worker queue.
static mx_status_t mxdm_worker_loop(mxdm_worker_t *worker);

////////
// Block I/O

// Creates a synchronous I/O transaction for use in mxdm_sync_io.
static mx_status_t mxdm_sync_init(mxdm_t *mxdm, uint64_t blkoff, size_t length,
                                  iotxn_t **out);
// Queues a synchronous I/O transaction created by mxdm_sync_init and waits for
// it to complete.
static mx_status_t mxdm_sync_io(mxdm_t *mxdm, iotxn_t *txn);
// Called when txn completes, this function signals the waiting caller to
// 'mxdm_sync_io'.
static void mxdm_sync_cb(iotxn_t *txn, void *cookie);

////////
// Block caching functions

// Initializes the block cache.
static void mxdm_cache_init(mxdm_worker_t *worker);
// Calculates the bucket number for a block at a given offset.
static uint32_t mxdm_get_bucket(uint64_t blkoff);

////////
// Bitmap functions

// Creates a new bitmap that can hold 'blklen' bits.  The bitmap is made up of
// 'chunks', each of which can either be a simple, uncompressed bitmap (raw) or
// a compressed, run-length encoding (RLE).  Initially, all chunks are RLE
// chunks. An RLE chunks is converted it would use more memory than a raw chunk.
static mxdm_bitmap_t *mxdm_bitmap_init(uint64_t bitlen);
// Releases memory associated with a bitmap.
static void mxdm_bitmap_free(mxdm_bitmap_t *bitmap);
// Releases memory associated with a specific RLE chunk.
static void mxdm_bitmap_rle_free(mxdm_bitmap_rle_t *rle);
// Returns true if the raw chunk at 'bitoff' would use less memory as an RLE
// chunk.
static bool mxdm_bitmap_raw_is_compressible(mxdm_bitmap_t *bitmap,
                                            uint64_t bitoff);
// Converts any raw chunks that would use less memory as RLE chunks to RLE
// chunks.
static void mxdm_compress_bitmap(mxdm_bitmap_t *bitmap);
// Convets a specific raw chunk into an RLE chunk.
static mx_status_t mxdm_bitmap_raw_to_rle(mxdm_bitmap_t *bitmap,
                                          uint64_t bitoff);
// Convets a specific RLE chunk into an raw chunk.
static mx_status_t mxdm_bitmap_rle_to_raw(mxdm_bitmap_t *bitmap,
                                          uint64_t bitoff);
// Adds a new RLE element to the RLE chunk.  This can fail if the RLE chunk has
// gotten too large, or is OOM.
static mx_status_t mxdm_bitmap_rle_add(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff, uint64_t bitlen,
                                       mxdm_bitmap_rle_elem_t **out);

// Returns true if a single bit in the 'bitmap' given by 'bitoff' is set.
static bool mxdm_bitmap_one(const mxdm_bitmap_t *bitmap, uint64_t bitoff);
// Returns true if a all the range of bits form 'bitoff' to 'bitmax' are set.
// Otherwise, it sets 'bitoff' to the first bit that is not set and returns
// false.
static bool mxdm_bitmap_get(const mxdm_bitmap_t *bitmap, uint64_t *bitoff,
                            uint64_t bitmax);
// Acts the same as mxdm_bitmap_get, but for a specific RLE chunk.
static bool mxdm_bitmap_rle_get(const mxdm_bitmap_t *bitmap, uint64_t chunk,
                                uint64_t *bitoff, uint64_t bitmax);
// Acts the same as mxdm_bitmap_get, but for a specific raw chunk.
static bool mxdm_bitmap_raw_get(const mxdm_bitmap_t *bitmap, uint64_t chunk,
                                uint64_t *bitoff, uint64_t bitmax);

// Sets the bit given by 'bitoff' in the 'bitmap'.
static mx_status_t mxdm_bitmap_set(mxdm_bitmap_t *bitmap, uint64_t bitoff);
// Acts the same as mxdm_bitmap_set, but for a specific RLE chunk.  This can
// fail if the RLE chunk has gotten too large, or is OOM.
static mx_status_t mxdm_bitmap_rle_set(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff);
// Acts the same as mxdm_bitmap_set, but for a specific raw chunk.
static mx_status_t mxdm_bitmap_raw_set(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff);

// Clears the bit given by 'bitoff' in 'bitmap'.
static mx_status_t mxdm_bitmap_clr(mxdm_bitmap_t *bitmap, uint64_t bitoff,
                                   uint64_t bitmax);
// Acts the same as mxdm_bitmap_clr, but for a specific RLE chunk.  This can
// fail if the RLE chunk has gotten too large, or is OOM.
static mx_status_t mxdm_bitmap_rle_clr(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff, uint64_t bitmax);
// Acts the same as mxdm_bitmap_clr, but for a specific raw chunk.
static mx_status_t mxdm_bitmap_raw_clr(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff, uint64_t bitmax);

////////
// I/O CTL protocol functions

// Process an I/O ctl.  It first calls the ioctl callback.  If that returns
// ERR_NOT_SUPPORTED, it tries to handle it locally.  If is does not recognize
// 'op' it finally passes it to the parent device.
static ssize_t mxdm_ioctl(mx_device_t *dev, uint32_t op, const void *in_buf,
                          size_t in_len, void *out_buf, size_t out_len);
// Always returns ERR_NOT_SUPPORTED.  This callback is used if none was is given
// in mxdm_init.
static ssize_t mxdm_default_ioctl(mxdm_t *mxdm, uint32_t op, const void *in_buf,
                                  size_t in_len, void *out_buf, size_t out_len);
// Can be called via I/O ctl or directly, this returns the usable size of the
// device (i.e. the aggregate size of the "data" blocks).
static mx_off_t mxdm_get_size(mx_device_t *dev);

////////
// I/O transaction protocol functions

// Attempts to add an I/O transaction to the worker's queue for processing.  If
// it fails (e.g. the worker is exiting), the transaction is passed to
// mxdm_complete_txn.
static void mxdm_iotxn_try_queue(mxdm_worker_t *worker, iotxn_t *txn);
// Accepts an external I/O transaction.  The iotxn is not immediately sent to
// the parent, but instead is put on the worker's queue for processing via
// mxdm_iotxn_try_queue.
static void mxdm_iotxn_queue(mx_device_t *dev, iotxn_t *txn);
// Creates an internal transaction clone from an external transaction, allowing
// the worker to own the txn and mangle it as needed.
static iotxn_t *mxdm_clone_txn(mxdm_worker_t *worker, iotxn_t *txn);
// Configures I/O completion callbacks for both cloned and cached iotxns.
static mx_status_t mxdm_set_callback(mxdm_worker_t *worker, iotxn_t *txn,
                                     void *origin);
// Always returns kMxdmContinueTxn.  This callback is used if none was given in
// mxdm_init.
static mxdm_txn_action_t mxdm_default_before(mxdm_worker_t *worker,
                                             iotxn_t *txn, uint64_t *blkoff,
                                             uint64_t blkmax);
// Places the transaction back onto the worker's queue after the I/O is
// complete.
static void mxdm_iotxn_cb(iotxn_t *txn, void *cookie);
// Always returns kMxdmCompleteTxn.  This callback is used if none was given in
// mxdm_init.
static mxdm_txn_action_t mxdm_default_after(mxdm_worker_t *worker, iotxn_t *txn,
                                            uint64_t *blkoff, uint64_t blkmax);
// Releases resources for internal I/O transactions, and calls the completion
// callback for external transactions.
static void mxdm_complete_txn(mxdm_worker_t *worker, iotxn_t *txn);

////////
// Tear-down protocol functions

// Notifies the worker that the device is going away.  All outstanding I/O
// transactions will eventually fail with ERR_HANDLE_CLOSED.
static void mxdm_unbind(mx_device_t *dev);
// Notifies the worker that is safe to begin tear-down and clean-up.  This
// function also detaches the worker.  The worker will asynchronously complete
// the clean-up (including the release callback) and then exit.
static mx_status_t mxdm_release(mx_device_t *dev);

////////////////
// Constants

static mx_protocol_device_t mxdm_proto = {
    .unbind = mxdm_unbind,
    .release = mxdm_release,
    .iotxn_queue = mxdm_iotxn_queue,
    .get_size = mxdm_get_size,
    .ioctl = mxdm_ioctl,
};

////////////////
// Functions

////////
// Constructor/destructor functions

mx_status_t mxdm_init(mx_driver_t *drv, mx_device_t *parent, const char *suffix,
                      const mxdm_ops_t *ops, size_t context_size) {
  MXDM_TRACE_INIT();
  mx_status_t rc = NO_ERROR;
  mxdm_t *mxdm = NULL;
  mxdm_init_info_t *info = NULL;
  MXDM_CHECK_NULL_RET(drv, ERR_INVALID_ARGS);
  MXDM_CHECK_NULL_RET(parent, ERR_INVALID_ARGS);
  MXDM_CHECK_NULL_RET(suffix, ERR_INVALID_ARGS);
  MXDM_CHECK_NULL_RET(ops, ERR_INVALID_ARGS);
  MXDM_CHECK_NULL_RET(ops->prepare, ERR_INVALID_ARGS);
  MXDM_CHECK_NULL_RET(ops->release, ERR_INVALID_ARGS);
  // Create the mxdm.
  mxdm = calloc(1, sizeof(mxdm_t) + context_size);
  info = calloc(1, sizeof(mxdm_init_info_t));
  if (!mxdm || !info) {
    MXDM_TRACE("out of memory!");
    return ERR_NO_MEMORY;
  }
  mxdm->ops.prepare = ops->prepare;
  mxdm->ops.ioctl = (ops->ioctl ? ops->ioctl : mxdm_default_ioctl);
  mxdm->ops.release = ops->release;
  mxdm->ops.before_read =
      (ops->before_read ? ops->before_read : mxdm_default_before);
  mxdm->ops.before_write =
      (ops->before_write ? ops->before_write : mxdm_default_before);
  mxdm->ops.after_read =
      (ops->after_read ? ops->after_read : mxdm_default_after);
  mxdm->ops.after_write =
      (ops->after_write ? ops->after_write : mxdm_default_after);
  mxdm->context_size = context_size;
  // Fill in the init info.
  info->drv = drv;
  info->dev = parent;
  snprintf(info->name, sizeof(info->name), "%s-%s", parent->name, suffix);
  info->mxdm = mxdm;
  // Create a detached thread that will cleanup after itself.  The thread
  // takes ownership of mxdm and info.
  mxdm_worker_t *worker = &mxdm->worker;
  if (thrd_create(&worker->thrd, mxdm_worker, info) != thrd_success) {
    MXDM_TRACE("thrd_create failed");
    mxdm_free(mxdm);
    free(info);
    return ERR_NO_RESOURCES;
  }
  // thrd_detach should only fail if the worker has already exited.
  if (thrd_detach(worker->thrd) != thrd_success) {
    MXDM_TRACE("thrd_detach failed");
    thrd_join(worker->thrd, &rc);
    return rc;
  }
  return NO_ERROR;
}

static mx_status_t mxdm_free(mxdm_t *mxdm) {
  mx_status_t rc = NO_ERROR;
  if (mxdm) {
    mxdm_bitmap_free(mxdm->worker.bitmap);
    free(mxdm);
  }
  return rc;
}

////////
// Helper functions

mxdm_t *mxdm_from_worker(mxdm_worker_t *worker) {
  MXDM_CHECK_NULL_RET(worker, NULL);
  return containerof(worker, mxdm_t, worker);
}

void *mxdm_get_context(mxdm_t *mxdm) {
  MXDM_CHECK_NULL_RET(mxdm, NULL);
  if (mxdm->context_size == 0) {
    return NULL;
  }
  return mxdm->context;
}

static mxdm_t *mxdm_from_device(mx_device_t *dev) {
  assert(dev);
  return containerof(dev, mxdm_t, dev);
}

////////
// Worker functions

static int mxdm_worker(void *arg) {
  assert(arg);
  mxdm_init_info_t *info = arg;
  mx_status_t rc = mxdm_worker_init(info);
  if (rc < 0) {
    driver_unbind(info->drv, info->dev);
  }
  mxdm_t *mxdm = info->mxdm;
  free(info);
  if (rc >= 0) {
    rc = mxdm_worker_loop(&mxdm->worker);
  }
  mxdm_free(mxdm);
  return rc;
}

static mx_status_t mxdm_worker_init(mxdm_init_info_t *info) {
  mx_status_t rc = NO_ERROR;
  assert(info);
  mxdm_t *mxdm = info->mxdm;
  mxdm_worker_t *worker = &mxdm->worker;
  mx_device_t *parent = info->dev;
  // Check block related sizes on parent.
  size_t size = 0;
  rc = parent->ops->ioctl(parent, IOCTL_BLOCK_GET_BLOCKSIZE, NULL, 0, &size,
                          sizeof(size));
  if (rc < 0) {
    MXDM_TRACE("get_blocksize ioctl failed: %d", rc);
    goto done;
  }
  if (MXDM_BLOCK_SIZE % size != 0) {
    MXDM_TRACE("invalid parent block size: %zu", size);
    rc = ERR_NOT_SUPPORTED;
    goto done;
  }
  size = parent->ops->get_size(parent);
  if (size == 0) {
    MXDM_TRACE("parent device is not seekable: %s", parent->name);
    rc = ERR_NOT_SUPPORTED;
    goto done;
  }
  // Set up the worker
  worker->state = kWorkerWorking;
  list_initialize(&worker->txns);
  mxdm_cache_init(worker);
  worker->bitmap = mxdm_bitmap_init(size / MXDM_BLOCK_SIZE);
  if (!worker->bitmap || mtx_init(&worker->mtx, mtx_plain) != thrd_success ||
      cnd_init(&worker->cnd) != thrd_success) {
    MXDM_TRACE("failed to init worker");
    rc = ERR_NO_RESOURCES;
    goto done;
  }
  // Next, configure the device in devmgr. Retrieve the parent stashed in
  // mxdm->dev.
  mtx_lock(&worker->mtx);
  device_init(&mxdm->dev, info->drv, info->name, &mxdm_proto);
  mxdm->dev.protocol_id = MX_PROTOCOL_BLOCK;
  // No multi-threading concerns until the device is added to the tree.
  rc = device_add(&mxdm->dev, parent);
  if (rc < 0) {
    MXDM_TRACE("device_add returned %d", rc);
    goto done;
  }
  // Use the "prepare" callback to do any asynchronous set-up.
  rc = mxdm->ops.prepare(worker, size / MXDM_BLOCK_SIZE, &mxdm->data_blkoff,
                         &mxdm->data_blklen);
  if (rc < 0) {
    MXDM_TRACE("prepare callback returned %d", rc);
    worker->state = kWorkerExiting;
  }
  mtx_unlock(&worker->mtx);
done:
  return rc;
}

static mx_status_t mxdm_worker_loop(mxdm_worker_t *worker) {
  assert(worker);
  mxdm_t *mxdm = mxdm_from_worker(worker);
  iotxn_t *txn;
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
          return mxdm->ops.release(worker);
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
    if (txn->context == mxdm) {
      txn = mxdm_clone_txn(worker, txn);
      if (!txn) {
        // mxdm_clone_txn calls mxdm_complete_txn on error.
        continue;
      }
    }
    mxdm_txn_cookie_t *c = txn->cookie;
    mxdm_txn_action_t next = kMxdmIgnoreTxn;
    MXDM_TRACE("processing iotxn: blkoff=%lu, blkmax=%lu", c->blkoff,
               c->blkmax);
    if (txn->actual == 0) {
      // I/O hasn't occurred yet.
      if (txn->opcode == IOTXN_OP_READ) {
        MXDM_TRACE("before before_read");
        next = mxdm->ops.before_read(worker, txn, &c->blkoff, c->blkmax);
        MXDM_TRACE("after before_read");
      } else {
        MXDM_TRACE("before before_write");
        next = mxdm->ops.before_write(worker, txn, &c->blkoff, c->blkmax);
        MXDM_TRACE("after before_write");
      }
    } else {
      // Cache the completed I/O, if appropriate.
      // if (!mxdm_is_data(worker, c->blkoff)) {
      //   mxdm_process_block(worker, c->origin.block);
      // }
      if (txn->opcode == IOTXN_OP_READ) {
        MXDM_TRACE("before after_read");
        next = mxdm->ops.after_read(worker, txn, &c->blkoff, c->blkmax);
        MXDM_TRACE("after after_read");
      } else {
        MXDM_TRACE("before after_write");
        next = mxdm->ops.after_write(worker, txn, &c->blkoff, c->blkmax);
        MXDM_TRACE("after after_write");
      }
    }
    MXDM_TRACE("iotxn processed: blkoff=%lu, blkmax=%lu", c->blkoff, c->blkmax);
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
        mx_device_t *parent = mxdm->dev.parent;
        c->blkoff = txn->offset / MXDM_BLOCK_SIZE;
        parent->ops->iotxn_queue(parent, txn);
        break;
      case kMxdmCompleteTxn:
      default:
        mxdm_complete_txn(worker, txn);
        break;
    }
    // Periodically reclaim memory from the bitmaps.
    counter = (counter + 1) & 0xFFFF;
    if (counter == 0) {
      mxdm_compress_bitmap(worker->bitmap);
    }
  }
}

////////
// Block I/O

mx_status_t mxdm_read(mxdm_t *mxdm, uint64_t blkoff, void *out, size_t length) {
  MXDM_CHECK_NULL_RET(mxdm, ERR_INVALID_ARGS);
  MXDM_CHECK_NULL_RET(out, ERR_INVALID_ARGS);
  iotxn_t *txn = NULL;
  mx_status_t rc = mxdm_sync_init(mxdm, blkoff, length, &txn);
  if (rc < 0) {
    return rc;
  }
  txn->opcode = IOTXN_OP_READ;
  rc = mxdm_sync_io(mxdm, txn);
  if (rc < 0) {
    return rc;
  }
  txn->ops->copyfrom(txn, out, 0, length);
  return NO_ERROR;
}

mx_status_t mxdm_write(mxdm_t *mxdm, uint64_t blkoff, const void *buffer,
                       size_t length) {
  MXDM_CHECK_NULL_RET(mxdm, ERR_INVALID_ARGS);
  MXDM_CHECK_NULL_RET(buffer, ERR_INVALID_ARGS);
  iotxn_t *txn = NULL;
  mx_status_t rc = mxdm_sync_init(mxdm, blkoff, length, &txn);
  if (rc < 0) {
    return rc;
  }
  txn->opcode = IOTXN_OP_WRITE;
  txn->ops->copyto(txn, buffer, 0, length);
  rc = mxdm_sync_io(mxdm, txn);
  if (rc < 0) {
    return rc;
  }
  return NO_ERROR;
}

static mx_status_t mxdm_sync_init(mxdm_t *mxdm, uint64_t blkoff, size_t length,
                                  iotxn_t **out_txn) {
  assert(mxdm);
  assert(out_txn);
  mx_device_t *parent = mxdm->dev.parent;
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
  iotxn_t *txn = NULL;
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

static mx_status_t mxdm_sync_io(mxdm_t *mxdm, iotxn_t *txn) {
  assert(mxdm);
  assert(txn);
  mx_device_t *parent = mxdm->dev.parent;
  completion_t completion = COMPLETION_INIT;
  txn->complete_cb = mxdm_sync_cb;
  txn->cookie = &completion;
  iotxn_queue(parent, txn);
  completion_wait(&completion, MX_TIME_INFINITE);
  if (txn->actual < txn->length) {
    MXDM_TRACE("incomplete I/O: only %lu of %lu", txn->actual, txn->length);
    return ERR_IO;
  }
  return txn->status;
}

static void mxdm_sync_cb(iotxn_t *txn, void *cookie) {
  completion_signal((completion_t *)cookie);
}

////////
// Block caching functions

static void mxdm_cache_init(mxdm_worker_t *worker) {
  list_initialize(&worker->queue);
  list_initialize(&worker->lru);
  for (size_t i = 0; i < MXDM_CACHE_SIZE; ++i) {
    mxdm_block_t *block = &worker->cache[i];
    list_initialize(&block->dependencies);
    list_add_tail(&worker->lru, &block->lru_node);
  }
  for (size_t i = 0; i < MXDM_NUM_BUCKETS; ++i) {
    list_initialize(&worker->buckets[i]);
  }
}

static uint32_t mxdm_get_bucket(uint64_t blkoff) {
  // Perform djb2a on the offset.
  uint32_t h = 5381;
  for (size_t i = 0; i < sizeof(uint64_t); ++i) {
    h = ((h << 5) + h) ^ ((blkoff >> i) & 0xFF);
  }
  return h & (MXDM_NUM_BUCKETS - 1);
}

mx_status_t mxdm_acquire_block(mxdm_worker_t *worker, uint64_t blkoff,
                               mxdm_block_t **out) {
  MXDM_CHECK_NULL_RET(worker, ERR_INVALID_ARGS);
  MXDM_CHECK_NULL_RET(out, ERR_INVALID_ARGS);
  mx_status_t rc = NO_ERROR;
  mxdm_block_t *block = NULL;
  // Look for a cached block in the buckets.
  uint32_t h = mxdm_get_bucket(blkoff);
  list_node_t *bucket = &worker->buckets[h];
  list_for_every_entry(bucket, block, mxdm_block_t, bucket_node) {
    if (block->txn->offset == blkoff * MXDM_BLOCK_SIZE) {
      MXDM_TRACE("found block in cache for %lu", blkoff);
      goto found;
    }
  }
  // Not found, try to grab a block from LRU list.
  block = list_remove_tail_type(&worker->lru, mxdm_block_t, lru_node);
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
  rc = mxdm_set_callback(worker, block->txn, block);
  if (rc < 0) {
    goto fail;
  }
  // Put the block in the bucket.
  if (list_in_list(&block->bucket_node)) {
    list_delete(&block->bucket_node);
  }
  list_add_head(bucket, &block->bucket_node);
  // Queue the txn.
  mxdm_iotxn_try_queue(worker, block->txn);
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
    list_add_tail(&worker->lru, &block->lru_node);
  }
  return rc;
}

bool mxdm_block_is_ready(const mxdm_block_t *block) {
  MXDM_CHECK_NULL_RET(block, false);
  return block->ready;
}

void mxdm_wait_for_block(mxdm_block_t *block, iotxn_t *txn) {
  MXDM_CHECK_NULL(block);
  MXDM_CHECK_NULL(txn);
  if (!block->ready && !list_in_list(&txn->node)) {
    list_add_tail(&block->dependencies, &txn->node);
  }
}

void mxdm_get_block(const mxdm_block_t *block, size_t offset, size_t length,
                    void *buffer) {
  MXDM_CHECK_NULL(block);
  MXDM_CHECK_NULL(buffer);
  block->txn->ops->copyfrom(block->txn, buffer, length, offset);
}

void mxdm_put_block(const void *buffer, size_t offset, size_t length,
                    mxdm_block_t *block) {
  MXDM_CHECK_NULL(block);
  MXDM_CHECK_NULL(buffer);
  block->txn->ops->copyto(block->txn, buffer, length, offset);
  block->dirty = 1;
}

void mxdm_release_block(mxdm_worker_t *worker, mxdm_block_t *block) {
  MXDM_CHECK_NULL(worker);
  MXDM_CHECK_NULL(block);
  --block->refcount;
  MXDM_TRACE("block %08x refcount decremented to %d",
             mxdm_get_bucket(block->txn->offset / MXDM_BLOCK_SIZE),
             block->refcount);
  if (block->refcount != 0) {
    return;
  }
  if (!block->dirty) {
    block->txn->ops->release(block->txn);
    list_add_head(&worker->lru, &block->lru_node);
    return;
  }
  // Re-purpose the read txn to write back the data.
  ++block->refcount;
  MXDM_TRACE("block %08x refcount incremented to %d",
             mxdm_get_bucket(block->txn->offset / MXDM_BLOCK_SIZE),
             block->refcount);
  block->ready = false;
  iotxn_t *block_txn = block->txn;
  block_txn->opcode = IOTXN_OP_WRITE;
  block_txn->actual = 0;
  block_txn->status = NO_ERROR;
  // Queue the txn.
  // TODO(aarongreen): Don't queue immediately, delay until something else is
  // dirtied/this block is being evicted/lru is running low?
  mxdm_iotxn_try_queue(worker, block_txn);
}

////////
// Block-marking functions

bool mxdm_is_data(mxdm_worker_t *worker, uint64_t blkoff) {
  MXDM_CHECK_NULL_RET(worker, false);
  mxdm_t *mxdm = mxdm_from_worker(worker);
  return mxdm->data_blkoff <= blkoff &&
         blkoff < mxdm->data_blkoff + mxdm->data_blklen;
}

bool mxdm_check_block(mxdm_worker_t *worker, uint64_t blkoff) {
  return mxdm_check_blocks(worker, &blkoff, blkoff + 1);
}

bool mxdm_check_blocks(mxdm_worker_t *worker, uint64_t *blkoff,
                       uint64_t blkmax) {
  MXDM_CHECK_NULL_RET(worker, false);
  MXDM_CHECK_NULL_RET(blkoff, false);
  return mxdm_bitmap_get(worker->bitmap, blkoff, blkmax);
}

mx_status_t mxdm_mark_block(mxdm_worker_t *worker, uint64_t blkoff) {
  MXDM_CHECK_NULL_RET(worker, ERR_INVALID_ARGS);
  return mxdm_bitmap_set(worker->bitmap, blkoff);
}

mx_status_t mxdm_clear_blocks(mxdm_worker_t *worker, uint64_t blkoff,
                              uint64_t blkmax) {
  MXDM_CHECK_NULL_RET(worker, ERR_INVALID_ARGS);
  return mxdm_bitmap_clr(worker->bitmap, blkoff, blkmax);
}

////////
// Bitmap functions

static mxdm_bitmap_t *mxdm_bitmap_init(uint64_t bitlen) {
  if (bitlen == 0) {
    MXDM_TRACE("invalid bitlen: 0");
    return NULL;
  }
  mxdm_bitmap_t *bitmap = calloc(1, sizeof(mxdm_bitmap_t));
  if (!bitmap) {
    MXDM_TRACE("out of memory!");
    goto fail;
  }
  bitmap->bitlen = bitlen;
  bitmap->chunks = ((bitlen - 1) / MXDM_BITS_PER_CHUNK) + 1;
  bitmap->data = calloc(bitmap->chunks, sizeof(void *));
  if (!bitmap->data) {
    MXDM_TRACE("out of memory!");
    goto fail;
  }
  // Handle devices with only a few blocks.
  if (bitmap->chunks == 1) {
    bitmap->data[0].raw = calloc(MXDM_BITS_PER_CHUNK / 64, sizeof(uint64_t));
    if (!bitmap->data[0].raw) {
      MXDM_TRACE("out of memory!");
      goto fail;
    }
    return bitmap;
  }
  // Handle larger devices.
  bitmap->use_rle = mxdm_bitmap_init(bitmap->chunks);
  for (size_t i = 0; i < bitmap->chunks; ++i) {
    mxdm_bitmap_rle_t *rle = calloc(1, sizeof(mxdm_bitmap_rle_t));
    if (!rle) {
      MXDM_TRACE("out of memory!");
      goto fail;
    }
    list_initialize(&rle->elems);
    rle->length = 0;
    if (mxdm_bitmap_set(bitmap->use_rle, i) < 0) {
      goto fail;
    }
    bitmap->data[i].rle = rle;
  }
  return bitmap;
fail:
  mxdm_bitmap_free(bitmap);
  return NULL;
}

static void mxdm_bitmap_free(mxdm_bitmap_t *bitmap) {
  if (!bitmap) {
    return;
  }
  MXDM_TRACE("freeing bitmap %p of length %lu", bitmap, bitmap->bitlen);
  uint64_t n = ((bitmap->bitlen - 1) / MXDM_BITS_PER_CHUNK) + 1;
  if (bitmap->data) {
    for (uint64_t i = 0; i < n;) {
      if (mxdm_bitmap_one(bitmap->use_rle, i)) {
        if (bitmap->data[i].rle) {
          mxdm_bitmap_rle_free(bitmap->data[i].rle);
        }
        bitmap->data[i].rle = NULL;
      } else {
        if (bitmap->data[i].raw) {
          free(bitmap->data[i].raw);
        }
        bitmap->data[i].raw = NULL;
      }
    }
    free(bitmap->data);
    bitmap->data = NULL;
  }
  mxdm_bitmap_free(bitmap->use_rle);
  bitmap->use_rle = NULL;
  free(bitmap);
}

static void mxdm_bitmap_rle_free(mxdm_bitmap_rle_t *rle) {
  if (!rle) {
    return;
  }
  // TODO(aarongreen): a lot of malloc overhead from small chunks; amortize with
  // a pool that can be refilled?
  mxdm_bitmap_rle_elem_t *elem = NULL;
  mxdm_bitmap_rle_elem_t *temp = NULL;
  list_for_every_entry_safe(&rle->elems, elem, temp, mxdm_bitmap_rle_elem_t,
                            node) {
    list_delete(&elem->node);
    --rle->length;
    free(elem);
  }
  assert(rle->length == 0);
  free(rle);
}

static inline void mxdm_bitmap_assert(const mxdm_bitmap_t *bitmap,
                                      uint64_t chunk) {
  assert(bitmap);
  assert(chunk < bitmap->chunks);
}

static inline void mxdm_bitmap_data_assert(const mxdm_bitmap_t *bitmap,
                                           uint64_t chunk,
                                           const uint64_t *bitoff,
                                           uint64_t bitmax) {
  assert(bitmap);
  assert(bitmax == 0 || chunk < bitmap->chunks);
  assert(bitoff);
  assert(*bitoff < MXDM_BITS_PER_CHUNK);
  assert(bitmax <= MXDM_BITS_PER_CHUNK);
}

static bool mxdm_bitmap_raw_is_compressible(mxdm_bitmap_t *bitmap,
                                            uint64_t chunk) {
  mxdm_bitmap_assert(bitmap, chunk);
  uint64_t *raw = bitmap->data[chunk].raw;
  uint8_t bitseq = 0;
  size_t num_elems = 0;
  size_t max_elems = (MXDM_BITS_PER_CHUNK / 8) / sizeof(mxdm_bitmap_rle_elem_t);
  for (uint64_t bitoff = 0; bitoff < MXDM_BITS_PER_CHUNK; ++bitoff) {
    // Shift previous bit up and add next from bitmap.  Works across
    // uint64_t boundaries.
    bitseq = ((bitseq & 1) << 1) | ((raw[bitoff / 64] >> (bitoff & 63)) & 1);
    // Only a bit sequence of "01" requires a new element.
    if (bitseq != 1) {
      continue;
    }
    ++num_elems;
    if (num_elems >= max_elems) {
      return false;
    }
  }
  return true;
}

static void mxdm_compress_bitmap(mxdm_bitmap_t *bitmap) {
  assert(bitmap);
  if (!bitmap->use_rle) {
    return;
  }
  mxdm_compress_bitmap(bitmap->use_rle);
  for (uint64_t i = 0; i < bitmap->chunks; ++i) {
    if (mxdm_bitmap_raw_is_compressible(bitmap, i)) {
      mxdm_bitmap_raw_to_rle(bitmap, i);
    }
  }
}

static mx_status_t mxdm_bitmap_raw_to_rle(mxdm_bitmap_t *bitmap,
                                          uint64_t chunk) {
  mxdm_bitmap_assert(bitmap, chunk);
  mx_status_t rc = NO_ERROR;
  uint64_t *raw = bitmap->data[chunk].raw;
  mxdm_bitmap_rle_t *rle = calloc(1, sizeof(mxdm_bitmap_rle_t));
  if (!rle) {
    MXDM_TRACE("out of memory!");
    rc = ERR_NO_MEMORY;
    goto fail;
  }
  mxdm_bitmap_rle_elem_t *elem = NULL;
  for (uint64_t bitoff = 0; bitoff < MXDM_BITS_PER_CHUNK; ++bitoff) {
    uint64_t bitend = bitoff;
    mxdm_bitmap_raw_get(bitmap, chunk, &bitend, MXDM_BITS_PER_CHUNK);
    if (bitoff == bitend) {
      // First bit was unset; move forward.
      continue;
    }
    rc = mxdm_bitmap_rle_add(bitmap, chunk, bitoff, bitend - bitoff, &elem);
    if (rc < 0) {
      goto fail;
    }
    list_add_tail(&rle->elems, &elem->node);
    ++rle->length;
  }
  rc = mxdm_bitmap_set(bitmap->use_rle, chunk);
  if (rc < 0) {
    goto fail;
  }
  free(raw);
  bitmap->data[chunk].rle = rle;
  return NO_ERROR;
fail:
  if (rle) {
    mxdm_bitmap_rle_free(rle);
  }
  return rc;
}

static mx_status_t mxdm_bitmap_rle_to_raw(mxdm_bitmap_t *bitmap,
                                          uint64_t chunk) {
  mxdm_bitmap_assert(bitmap, chunk);
  mx_status_t rc = NO_ERROR;
  mxdm_bitmap_rle_t *rle = bitmap->data[chunk].rle;
  uint64_t *raw = calloc(MXDM_BITS_PER_CHUNK / 64, sizeof(uint64_t));
  if (!raw) {
    // Retry once
    mxdm_compress_bitmap(bitmap);
    raw = calloc(MXDM_BITS_PER_CHUNK / 64, sizeof(uint64_t));
  }
  if (!raw) {
    MXDM_TRACE("out of memory!");
    rc = ERR_NO_MEMORY;
    goto fail;
  }
  // Record that we're using a raw chunk now.
  rc = mxdm_bitmap_clr(bitmap->use_rle, chunk, chunk + 1);
  if (rc < 0) {
    goto fail;
  }
  bitmap->data[chunk].raw = raw;
  // It's easier to set all true and poke holes with existing functions.
  memset(raw, 0xFF, MXDM_BITS_PER_CHUNK / 8);
  uint64_t bitoff = 0;
  mxdm_bitmap_rle_elem_t *elem = NULL;
  list_for_every_entry(&rle->elems, elem, mxdm_bitmap_rle_elem_t, node) {
    rc = mxdm_bitmap_raw_clr(bitmap, chunk, bitoff, elem->bitoff);
    if (rc < 0) {
      goto fail;
    }
    bitoff = elem->bitoff + elem->bitlen;
  }
  rc = mxdm_bitmap_raw_clr(bitmap, chunk, bitoff, MXDM_BITS_PER_CHUNK);
  if (rc < 0) {
    goto fail;
  }
  //
  mxdm_bitmap_rle_free(rle);
  return NO_ERROR;
fail:
  // Restore the RLE chunk and clean up.
  mxdm_bitmap_set(bitmap->use_rle, chunk);
  bitmap->data[chunk].rle = rle;
  if (raw) {
    free(raw);
  }
  return rc;
}

static mx_status_t mxdm_bitmap_rle_add(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff, uint64_t bitlen,
                                       mxdm_bitmap_rle_elem_t **out) {
  mxdm_bitmap_assert(bitmap, chunk);
  assert(out);
  mxdm_bitmap_rle_t *rle = bitmap->data[chunk].rle;
  mxdm_bitmap_rle_elem_t *elem = NULL;
  if (rle->length >= MXDM_BLOCK_SIZE / sizeof(mxdm_bitmap_rle_elem_t)) {
    return ERR_OUT_OF_RANGE;
  }
  elem = calloc(1, sizeof(mxdm_bitmap_rle_elem_t));
  if (!elem) {
    // Retry once.
    mxdm_compress_bitmap(bitmap);
    elem = calloc(1, sizeof(mxdm_bitmap_rle_elem_t));
  }
  if (!elem) {
    MXDM_TRACE("out of memory!");
    return ERR_NO_MEMORY;
  }
  elem->bitoff = bitoff;
  elem->bitlen = bitlen;
  *out = elem;
  return NO_ERROR;
}

static bool mxdm_bitmap_one(const mxdm_bitmap_t *bitmap, uint64_t bitoff) {
  return mxdm_bitmap_get(bitmap, &bitoff, bitoff + 1);
}

static bool mxdm_bitmap_get(const mxdm_bitmap_t *bitmap, uint64_t *bitoff,
                            uint64_t bitmax) {
  MXDM_CHECK_NULL_RET(bitoff, false);
  // It's not an error to have a NULL bitmap here; use_rle often is.
  if (!bitmap || *bitoff >= bitmap->bitlen) {
    return false;
  }
  if (*bitoff >= bitmax) {
    return true;
  }
  uint64_t i = *bitoff / MXDM_BITS_PER_CHUNK;
  uint64_t n = MIN(((bitmax - 1) / MXDM_BITS_PER_CHUNK) + 1, bitmap->chunks);
  uint64_t off = *bitoff % MXDM_BITS_PER_CHUNK;
  uint64_t max = MXDM_BITS_PER_CHUNK;
  bool result = true;
  for (; i < n && result; ++i) {
    if (i == n - 1) {
      max = ((bitmax - 1) % MXDM_BITS_PER_CHUNK) + 1;
    }
    *bitoff -= off;
    if (mxdm_bitmap_one(bitmap->use_rle, i)) {
      result = mxdm_bitmap_rle_get(bitmap, i, &off, max);
    } else {
      result = mxdm_bitmap_raw_get(bitmap, i, &off, max);
    }
    *bitoff += off;
    off = 0;
  }
  return (*bitoff == bitmax);
}

static bool mxdm_bitmap_rle_get(const mxdm_bitmap_t *bitmap, uint64_t chunk,
                                uint64_t *bitoff, uint64_t bitmax) {
  mxdm_bitmap_data_assert(bitmap, chunk, bitoff, bitmax);
  const mxdm_bitmap_rle_t *rle = bitmap->data[chunk].rle;
  mxdm_bitmap_rle_elem_t *elem;
  list_for_every_entry(&rle->elems, elem, mxdm_bitmap_rle_elem_t, node) {
    if (*bitoff < elem->bitoff) {
      break;
    }
    if (*bitoff < elem->bitoff + elem->bitlen) {
      *bitoff = elem->bitoff + elem->bitlen;
      break;
    }
  }
  if (*bitoff > bitmax) {
    *bitoff = bitmax;
  }
  return *bitoff == bitmax;
}

#include <limits.h>
#if UINT64_MAX == UINT_MAX
#define CLZ64(u64) (u64 == 0 ? 64 : __builtin_clz(u64))
#elif UINT64_MAX == ULONG_MAX
#define CLZ64(u64) (u64 == 0 ? 64 : __builtin_clzl(u64))
#elif UINT64_MAX == ULLONG_MAX
#define CLZ64(u64) (u64 == 0 ? 64 : __builtin_clzll(u64))
#else
#error "Could not determine correct CLZ64 macro"
#endif
static bool mxdm_bitmap_raw_get(const mxdm_bitmap_t *bitmap, uint64_t chunk,
                                uint64_t *bitoff, uint64_t bitmax) {
  mxdm_bitmap_data_assert(bitmap, chunk, bitoff, bitmax);
  const uint64_t *raw = bitmap->data[chunk].raw;
  uint64_t i = *bitoff / 64;
  uint64_t n = ((bitmax - 1) / 64) + 1;
  uint64_t val = ~raw[i] << (*bitoff % 64);
  if (val != 0) {
    *bitoff = MIN(*bitoff + CLZ64(val), bitmax);
    return *bitoff == bitmax;
  }
  for (++i; i < n; ++i) {
    if (~raw[i] != 0) {
      *bitoff = ((i - 1) * 64) + CLZ64(~raw[i]);
      break;
    }
  }
  if (i == n || *bitoff > bitmax) {
    *bitoff = bitmax;
  }
  return *bitoff == bitmax;
}
#undef CLZ64

static mx_status_t mxdm_bitmap_set(mxdm_bitmap_t *bitmap, uint64_t bitoff) {
  MXDM_CHECK_NULL_RET(bitmap, ERR_INVALID_ARGS);
  if (bitoff >= bitmap->bitlen) {
    MXDM_TRACE("out of range: %lu", bitoff);
    return ERR_INVALID_ARGS;
  }
  uint64_t chunk = bitoff / MXDM_BITS_PER_CHUNK;
  bitoff %= MXDM_BITS_PER_CHUNK;
  mx_status_t rc = NO_ERROR;
  if (mxdm_bitmap_one(bitmap->use_rle, chunk)) {
    rc = mxdm_bitmap_rle_set(bitmap, chunk, bitoff);
    if (rc != ERR_OUT_OF_RANGE) {
      return rc;
    }
    rc = mxdm_bitmap_rle_to_raw(bitmap, chunk);
  }
  if (rc == NO_ERROR) {
    rc = mxdm_bitmap_raw_set(bitmap, chunk, bitoff);
  }
  return rc;
}

static mx_status_t mxdm_bitmap_rle_set(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff) {
  mxdm_bitmap_data_assert(bitmap, chunk, &bitoff, MXDM_BITS_PER_CHUNK);
  mx_status_t rc = NO_ERROR;
  mxdm_bitmap_rle_t *rle = bitmap->data[chunk].rle;
  mxdm_bitmap_rle_elem_t *elem = NULL;
  // Special case: empty list
  if (list_is_empty(&rle->elems)) {
    rc = mxdm_bitmap_rle_add(bitmap, chunk, bitoff, 1, &elem);
    if (rc < 0) {
      return rc;
    }
    list_add_tail(&rle->elems, &elem->node);
    ++rle->length;
    return NO_ERROR;
  }
  // General case: We iterate until we find an element that starts after bitoff,
  // or we run out of elements.
  mxdm_bitmap_rle_elem_t *prev = NULL;
  list_for_every_entry(&rle->elems, elem, mxdm_bitmap_rle_elem_t, node) {
    // Bit is right before elem
    if (bitoff + 1 == elem->bitoff) {
      if (prev) {
        // prev and elem can be merged.
        prev->bitlen += elem->bitlen;
        list_delete(&elem->node);
        --rle->length;
        free(elem);
      } else {
        // Extend elem
        --elem->bitoff;
        ++elem->bitlen;
      }
      return NO_ERROR;
    }
    // There's a gap between the bit and the next element.
    if (bitoff < elem->bitoff) {
      rc = mxdm_bitmap_rle_add(bitmap, chunk, bitoff, 1, &prev);
      if (rc < 0) {
        return rc;
      }
      list_add_before(&elem->node, &prev->node);
      ++rle->length;
      return NO_ERROR;
    }
    // Bit is already in an element.
    if (bitoff < elem->bitoff + elem->bitlen) {
      return NO_ERROR;
    }
    // Bit is right at the end of an element.
    if (bitoff == elem->bitoff + elem->bitlen) {
      ++elem->bitlen;
      // We might merge with the next RLE. Save and keep going.
      prev = elem;
    }
  }
  // We ran out of elements.  Add one at the end.
  rc = mxdm_bitmap_rle_add(bitmap, chunk, bitoff, 1, &elem);
  if (rc < 0) {
    return rc;
  }
  list_add_tail(&rle->elems, &elem->node);
  ++rle->length;
  return NO_ERROR;
}

static mx_status_t mxdm_bitmap_raw_set(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff) {
  mxdm_bitmap_data_assert(bitmap, chunk, &bitoff, MXDM_BITS_PER_CHUNK);
  uint64_t *raw = bitmap->data[chunk].raw;
  raw[bitoff >> 6] |= 0x8000000000000000ULL >> (bitoff & 63);
  return NO_ERROR;
}

static mx_status_t mxdm_bitmap_clr(mxdm_bitmap_t *bitmap, uint64_t bitoff,
                                   uint64_t bitmax) {
  MXDM_CHECK_NULL_RET(bitmap, ERR_INVALID_ARGS);
  mx_status_t rc = NO_ERROR;
  if (bitoff >= bitmax) {
    return NO_ERROR;
  }
  uint64_t i = bitoff / MXDM_BITS_PER_CHUNK;
  uint64_t n = MIN(((bitmax - 1) / MXDM_BITS_PER_CHUNK) + 1, bitmap->chunks);
  uint64_t off = bitoff % MXDM_BITS_PER_CHUNK;
  uint64_t max = MXDM_BITS_PER_CHUNK;
  for (; i < n && rc == NO_ERROR; ++i) {
    if (i == n - 1) {
      max = ((bitmax - 1) % MXDM_BITS_PER_CHUNK) + 1;
    }
    if (mxdm_bitmap_one(bitmap->use_rle, i)) {
      rc = mxdm_bitmap_rle_clr(bitmap, i, off, max);
      if (rc != ERR_OUT_OF_RANGE) {
        continue;
      }
      rc = mxdm_bitmap_rle_to_raw(bitmap, i);
    }
    if (rc == NO_ERROR) {
      rc = mxdm_bitmap_raw_clr(bitmap, i, off, max);
    }
    off = 0;
  }
  return rc;
}

static mx_status_t mxdm_bitmap_rle_clr(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff, uint64_t bitmax) {
  mxdm_bitmap_data_assert(bitmap, chunk, &bitoff, bitmax);
  mx_status_t rc = NO_ERROR;
  mxdm_bitmap_rle_t *rle = bitmap->data[chunk].rle;
  mxdm_bitmap_rle_elem_t *elem = NULL;
  mxdm_bitmap_rle_elem_t *temp = NULL;
  list_for_every_entry_safe(&rle->elems, elem, temp, mxdm_bitmap_rle_elem_t,
                            node) {
    if (elem->bitoff + elem->bitlen < bitoff) {
      continue;
    }
    if (bitmax < elem->bitoff) {
      break;
    }
    if (elem->bitoff < bitoff) {
      if (elem->bitoff + elem->bitlen < bitmax) {
        // 'elem' contains 'bitoff'.
        elem->bitlen = bitoff - elem->bitoff;
        break;
      } else {
        // 'elem' contains [bitoff, bitmax).
        elem->bitlen = bitoff - elem->bitoff;
        rc = mxdm_bitmap_rle_add(bitmap, chunk, bitmax,
                                 elem->bitoff + elem->bitlen - bitmax, &temp);
        if (rc < 0) {
          return rc;
        }
        list_add_after(&elem->node, &temp->node);
        ++rle->length;
        break;
      }
    } else {
      if (bitmax < elem->bitoff + elem->bitlen) {
        // 'elem' contains 'bitmax'
        elem->bitoff = bitmax;
        elem->bitlen = elem->bitoff + elem->bitlen - bitmax;
        break;
      } else {
        // [bitoff, bitmax) fully contains 'elem'.
        list_delete(&elem->node);
        free(elem);
        --rle->length;
      }
    }
  }
  return NO_ERROR;
}

static mx_status_t mxdm_bitmap_raw_clr(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff, uint64_t bitmax) {
  mxdm_bitmap_data_assert(bitmap, chunk, &bitoff, bitmax);
  uint64_t *raw = bitmap->data[chunk].raw;
  uint64_t i = bitoff / 64;
  uint64_t n = bitmax / 64;
  if (i == n && (bitmax % 64) != 0) {
    raw[i] &= ~((~0ULL) >> (bitoff % 64) & (~0ULL) << (64 - (bitmax % 64)));
    return NO_ERROR;
  }
  if (bitoff % 64 != 0) {
    raw[i++] &= (~0ULL) << (64 - bitoff % 64);
  }
  while (i < n) {
    raw[i++] = 0;
  }
  if (bitmax % 64 != 0) {
    raw[i] &= (~0ULL) >> (bitmax % 64);
  }
  return NO_ERROR;
}

////////
// I/O CTL protocol functions

static ssize_t mxdm_ioctl(mx_device_t *dev, uint32_t op, const void *in_buf,
                          size_t in_len, void *out_buf, size_t out_len) {
  mxdm_t *mxdm = mxdm_from_device(dev);
  ssize_t rc = mxdm->ops.ioctl(mxdm, op, in_buf, in_len, out_buf, out_len);
  if (rc != ERR_NOT_SUPPORTED) {
    return rc;
  }
  switch (op) {
    case IOCTL_BLOCK_GET_SIZE: {
      uint64_t *size = out_buf;
      if (!size || out_len < sizeof(*size)) {
        return ERR_NOT_ENOUGH_BUFFER;
      }
      *size = mxdm_get_size(dev);
      return sizeof(*size);
    }
    case IOCTL_BLOCK_GET_BLOCKSIZE: {
      uint64_t *blksize = out_buf;
      if (!blksize || out_len < sizeof(*blksize)) {
        return ERR_NOT_ENOUGH_BUFFER;
      }
      *blksize = MXDM_BLOCK_SIZE;
      return sizeof(*blksize);
    }
    default: {
      mx_device_t *parent = dev->parent;
      return parent->ops->ioctl(parent, op, in_buf, in_len, out_buf, out_len);
    }
  }
}

static ssize_t mxdm_default_ioctl(mxdm_t *mxdm, uint32_t op, const void *in_buf,
                                  size_t in_len, void *out_buf,
                                  size_t out_len) {
  return ERR_NOT_SUPPORTED;
}

static mx_off_t mxdm_get_size(mx_device_t *dev) {
  mxdm_t *mxdm = mxdm_from_device(dev);
  return mxdm->data_blklen * MXDM_BLOCK_SIZE;
}

////////
// I/O transaction protocol functions

static void mxdm_iotxn_try_queue(mxdm_worker_t *worker, iotxn_t *txn) {
  bool queued = false;
  bool is_data = mxdm_is_data(worker, txn->offset / MXDM_BLOCK_SIZE);
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

static void mxdm_iotxn_queue(mx_device_t *dev, iotxn_t *txn) {
  if (!txn || !dev) {
    return;
  }
  if (txn->length == 0) {
    txn->ops->complete(txn, ERR_INVALID_ARGS, 0);
    return;
  }
  mxdm_t *mxdm = mxdm_from_device(dev);
  mxdm_worker_t *worker = &mxdm->worker;
  txn->context = mxdm;
  mxdm_iotxn_try_queue(worker, txn);
}

static iotxn_t *mxdm_clone_txn(mxdm_worker_t *worker, iotxn_t *txn) {
  assert(worker);
  mxdm_t *mxdm = mxdm_from_worker(worker);
  assert(txn);
  assert(txn->context == mxdm);
  size_t data_offset = mxdm->data_blkoff * MXDM_BLOCK_SIZE;
  size_t data_length = mxdm->data_blklen * MXDM_BLOCK_SIZE;
  if (txn->offset % MXDM_BLOCK_SIZE != 0 ||
      txn->length % MXDM_BLOCK_SIZE != 0 || txn->offset >= data_length) {
    MXDM_TRACE("invalid txn: offset=%lu, length=%lu", txn->offset, txn->length);
    txn->status = ERR_INVALID_ARGS;
    mxdm_complete_txn(worker, txn);
    return NULL;
  }
  // Clone the txn and take ownership of it.
  iotxn_t *cloned = NULL;
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
  cloned->status = mxdm_set_callback(worker, cloned, txn);
  if (cloned->status != NO_ERROR) {
    mxdm_complete_txn(worker, cloned);
    return NULL;
  }
  return cloned;
}

static mx_status_t mxdm_set_callback(mxdm_worker_t *worker, iotxn_t *txn,
                                     void *origin) {
  assert(worker);
  assert(txn);
  assert(origin);
  txn->complete_cb = mxdm_iotxn_cb;
  mxdm_txn_cookie_t *c = calloc(1, sizeof(mxdm_txn_cookie_t));
  if (!c) {
    MXDM_TRACE("out of memory!");
    return ERR_NO_MEMORY;
  }
  c->worker = worker;
  // Is this a cache load, or a cloned read?
  if (!mxdm_is_data(worker, txn->offset / MXDM_BLOCK_SIZE) &&
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

static mxdm_txn_action_t mxdm_default_before(mxdm_worker_t *worker,
                                             iotxn_t *txn, uint64_t *blkoff,
                                             uint64_t blkmax) {
  assert(blkoff);
  *blkoff = blkmax;
  return kMxdmContinueTxn;
}

static void mxdm_iotxn_cb(iotxn_t *txn, void *cookie) {
  assert(cookie);
  mxdm_txn_cookie_t *c = cookie;
  mxdm_worker_t *worker = c->worker;
  mxdm_iotxn_try_queue(worker, txn);
}

static mxdm_txn_action_t mxdm_default_after(mxdm_worker_t *worker, iotxn_t *txn,
                                            uint64_t *blkoff, uint64_t blkmax) {
  assert(blkoff);
  *blkoff = blkmax;
  return kMxdmCompleteTxn;
}

static void mxdm_complete_txn(mxdm_worker_t *worker, iotxn_t *txn) {
  mxdm_t *mxdm = mxdm_from_worker(worker);
  assert(txn);
  if (txn->context == mxdm) {
    // txn is an original transaction.
    MXDM_TRACE("completing external iotxn for data block %lu",
               txn->offset / MXDM_BLOCK_SIZE);
    txn->context = NULL;
    if (list_in_list(&txn->node)) {
      list_delete(&txn->node);
    }
    txn->ops->complete(txn, txn->status, txn->actual);
  } else if (mxdm_is_data(worker, txn->offset / MXDM_BLOCK_SIZE)) {
    // txn is a clone, cookie has the external txn.
    MXDM_TRACE("completing cloned iotxn for raw block %lu",
               txn->offset / MXDM_BLOCK_SIZE);
    iotxn_t *cloned = txn;
    mxdm_txn_cookie_t *c = cloned->cookie;
    txn = c->origin.txn;
    txn->status = cloned->status;
    txn->actual = cloned->actual;
    cloned->ops->release(cloned);
    mxdm_complete_txn(worker, txn);
  } else {
    // txn is a cache-load, cookie has the cache block.
    MXDM_TRACE("completing cache iotxn for metadata block %lu",
               txn->offset / MXDM_BLOCK_SIZE);
    mxdm_txn_cookie_t *c = txn->cookie;
    mxdm_block_t *block = c->origin.block;
    if (txn->status == NO_ERROR && txn->actual == txn->length) {
      block->ready = true;
    }
    iotxn_t *tmp = NULL;
    iotxn_t *dep = NULL;
    list_for_every_entry_safe(&block->dependencies, dep, tmp, iotxn_t, node) {
      list_delete(&dep->node);
      mxdm_iotxn_try_queue(worker, dep);
    }
    if (block->dirty) {
      block->dirty = false;
      mxdm_release_block(worker, block);
    }
  }
}

////////
// Tear-down protocol functions

static void mxdm_unbind(mx_device_t *dev) {
  mxdm_t *mxdm = mxdm_from_device(dev);
  mxdm_worker_t *worker = &mxdm->worker;
  mtx_lock(&worker->mtx);
  worker->state = kWorkerStopping;
  cnd_broadcast(&worker->cnd);
  mtx_unlock(&worker->mtx);
}

static mx_status_t mxdm_release(mx_device_t *dev) {
  mxdm_t *mxdm = mxdm_from_device(dev);
  mx_status_t rc = NO_ERROR;
  mxdm_worker_t *worker = &mxdm->worker;
  mx_device_t *child = NULL;
  mx_device_t *temp = NULL;
  list_for_every_entry_safe(&dev->children, child, temp, mx_device_t, node) {
    device_remove(child);
  }
  mtx_lock(&worker->mtx);
  worker->state = kWorkerExiting;
  cnd_broadcast(&worker->cnd);
  mtx_unlock(&worker->mtx);
  return rc;
}
