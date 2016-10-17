// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file unit tests the MXDM framework code by using a dummy device, devmgr,
// and libmagenta to create several filter drivers that examine the frameworks
// behavior in their callbacks.

#include "mxdm.h"
#include "private.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/ioctl.h>
#include <ddk/iotxn.h>
#include <magenta/device/block.h>
#include <unittest/unittest.h>
#include <magenta/fuchsia-types.h>
#include <magenta/types.h>

// Constants

static const mx_time_t kTimeout = MX_SEC(3);
static const uint64_t kGuid = 0x0F1E2D3C4B5A6978;

// Macros

// Wraps BEGIN TEST with some additional MXDM set up
#define MXDM_BEGIN_TEST(ctx)                 \
  BEGIN_TEST;                                \
  if (!mxdm_test_setup(ctx, __FUNCTION__)) { \
    return false;                            \
  }

// Wraps END_TEST TEST with some additional MXDM tear down
#define MXDM_END_TEST(ctx) \
  mxdm_test_teardown(ctx); \
  END_TEST;

// Forward declarations

// Prints the name of the error code.
static void mxdm_test_print_rc(mx_status_t rc);

// Variables

// Used to gate the worker and/or test threads to make things more deterministic.
static struct mxdm_test_sync {
  mtx_t mtx;
  cnd_t cnd;
  bool signal;
} g_sync;

// Public functions

void mxdm_test_compare_rc(mx_status_t actual, mx_status_t expected) {
  if (actual != expected) {
    unittest_printf("        Expected ");
    mxdm_test_print_rc(expected);
    unittest_printf(" but got ");
    mxdm_test_print_rc(actual);
    unittest_printf("\n");
  }
}

bool mxdm_test_sync_init(void) {
  return mtx_init(&g_sync.mtx, mtx_plain) == thrd_success &&
         cnd_init(&g_sync.cnd) == thrd_success;
}

void mxdm_test_sync_wait(void) {
  mtx_lock(&g_sync.mtx);
  while (!g_sync.signal) {
    cnd_wait(&g_sync.cnd, &g_sync.mtx);
  }
  g_sync.signal = false;
  mtx_unlock(&g_sync.mtx);
}

void mxdm_test_sync_wake(void) {
  mtx_lock(&g_sync.mtx);
  g_sync.signal = true;
  cnd_broadcast(&g_sync.cnd);
  mtx_unlock(&g_sync.mtx);
}

// Private functions

// Utilities

static void mxdm_test_print_rc(mx_status_t rc) {
  switch (rc) {
    case NO_ERROR:
      unittest_printf("NO_ERROR");
      break;
#define FUCHSIA_ERROR(label, ignored)  \
  case ERR_##label:                    \
    unittest_printf("ERR_%s", #label); \
    break;
#include <magenta/fuchsia-types.def>
#undef FUCHSIA_ERROR
    default:
      unittest_printf("ERR_UNKNOWN");
      break;
  }
}

// Set up and tear down

static bool mxdm_test_setup(mxdm_test_ctx_t *ctx, const char *test_name) {
  if (!ctx || !mxdm_test_sync_init()) {
    return false;
  }
  ctx->guid = kGuid;
  list_initialize(&ctx->txns);
  mxmd_test_init_parent(&ctx->parent, ctx);
  mx_status_t rc = mxdm_init(&ctx->driver, &ctx->parent, test_name,
                             &ctx->device_ops, &ctx->worker_ops, 0);
  EXPECT_RC(rc, NO_ERROR, "mxdm_init");
  if (rc < 0) {
    return false;
  }
  mxdm_test_sync_wait();  // Should be signaled by mxdm_test_prepare.
  return ctx->bound;
}

static void mxdm_test_teardown(mxdm_test_ctx_t *ctx) {
  if (!ctx) {
    return;
  }
  mx_device_t *device = ctx->device;
  device->ops->unbind(device);
  device->ops->release(device);
  mxdm_test_sync_wait();  // Should be signaled by mxdm_test_release.
}

// MXDM callbacks

static mx_status_t mxdm_test_prepare(mxdm_worker_t *worker, uint64_t blklen,
                                     uint64_t *data_blkoff,
                                     uint64_t *data_blklen) {
  mx_status_t rc = NO_ERROR;
  EXPECT_NEQ(worker, NULL, "worker");
  EXPECT_NEQ(data_blkoff, NULL, "data_blkoff");
  EXPECT_NEQ(data_blklen, NULL, "data_blklen");
  if (!worker || !data_blkoff || !data_blklen) {
    rc = ERR_INTERNAL;
    goto done;
  }
  EXPECT_EQ(blklen, (size_t) MXDM_TEST_BLOCKS, "wrong number of blocks");
  *data_blkoff = 1;
  *data_blklen = MXDM_TEST_BLOCKS - 1;
done:
  mxdm_test_sync_wake();  // Signals MXDM_BEGIN_TEST
  return rc;
}

static mx_status_t mxdm_test_release(mxdm_worker_t *worker) {
  mxdm_test_sync_wake();  // Signals MXDM_END_TEST
  return NO_ERROR;
}

static mxdm_test_ctx_t *mxdm_test_ctx_init(void) {
  mxdm_test_ctx_t *ctx = calloc(1, sizeof(mxdm_test_ctx_t));
  if (!ctx) {
    return NULL;
  }
  ctx->guid = kGuid;
  list_initialize(&ctx->txns);
  mxmd_test_init_parent(&ctx->parent, ctx);
  ctx->worker_ops.prepare = mxdm_test_prepare;
  ctx->worker_ops.release = mxdm_test_release;
  return ctx;
}

// Unit tests

// test_get_size
static bool test_get_size(void) {
  mxdm_test_ctx_t *ctx = mxdm_test_ctx_init();
  MXDM_BEGIN_TEST(ctx);
  EXPECT_EQ(ctx->device->ops->get_size(ctx->device),
            (size_t) (MXDM_TEST_BLOCKS - 1) * MXDM_BLOCK_SIZE, "get_size");
  MXDM_END_TEST(ctx);
}

// test_ioctl
static const char *kName = "mxdm-test-device";
static ssize_t mxdm_ioctl(mxdm_device_t *device, uint32_t op,
                          const void *in_buf, size_t in_len, void *out_buf,
                          size_t out_len) {
  EXPECT_NEQ(device, NULL, "device");
  if (!device) {
    return ERR_INTERNAL;
  }
  switch (op) {
    case IOCTL_BLOCK_GET_NAME:
      if (!out_buf || out_len < MX_DEVICE_NAME_MAX + 1) {
        return ERR_BUFFER_TOO_SMALL;
      }
      char *name = out_buf;
      strncpy(name, kName, MX_DEVICE_NAME_MAX + 1);
      return MX_DEVICE_NAME_MAX + 1;
    default:
      return ERR_NOT_SUPPORTED;
  }
}

static bool test_ioctl(void) {
  mxdm_test_ctx_t *ctx = mxdm_test_ctx_init();
  ctx->device_ops.ioctl = mxdm_ioctl;
  MXDM_BEGIN_TEST(ctx);
  // Test IOCTLs handled by MXDM device
  char name[MX_DEVICE_NAME_MAX + 1] = {0};
  EXPECT_RC(ctx->device->ops->ioctl(ctx->device, IOCTL_BLOCK_GET_NAME, NULL, 0,
                                    NULL, sizeof(name)),
            ERR_BUFFER_TOO_SMALL, "GET_NAME ioctl with NULL buffer");
  EXPECT_RC(ctx->device->ops->ioctl(ctx->device, IOCTL_BLOCK_GET_NAME, NULL, 0,
                                    name, sizeof(name) - 1),
            ERR_BUFFER_TOO_SMALL, "GET_NAME ioctl with small buffer");
  EXPECT_GT(ctx->device->ops->ioctl(ctx->device, IOCTL_BLOCK_GET_NAME, NULL, 0,
                                    name, sizeof(name)),
            0, "GET_NAME ioctl failed");
  EXPECT_RC(strncmp(name, kName, MX_DEVICE_NAME_MAX), 0, "wrong name");
  // Test IOCTLs handled by MXDM framework
  size_t size = 0;
  EXPECT_RC(ctx->device->ops->ioctl(ctx->device, IOCTL_BLOCK_GET_BLOCKSIZE,
                                    NULL, 0, NULL, sizeof(size)),
            ERR_BUFFER_TOO_SMALL, "GET_BLOCKSIZE ioctl with NULL buffer");
  EXPECT_RC(ctx->device->ops->ioctl(ctx->device, IOCTL_BLOCK_GET_BLOCKSIZE,
                                    NULL, 0, &size, sizeof(size) - 1),
            ERR_BUFFER_TOO_SMALL, "GET_BLOCKSIZE ioctl with small buffer");
  EXPECT_GT(ctx->device->ops->ioctl(ctx->device, IOCTL_BLOCK_GET_BLOCKSIZE,
                                    NULL, 0, &size, sizeof(size)),
            0, "GET_BLOCKSIZE ioctl failed");
  EXPECT_EQ(size, (size_t)MXDM_BLOCK_SIZE, "wrong block size");
  // Test IOCTLs handled by parent (fake) device.
  uint64_t guid = 0;
  EXPECT_RC(ctx->device->ops->ioctl(ctx->device, IOCTL_BLOCK_GET_GUID, NULL, 0,
                                    NULL, sizeof(guid)),
            ERR_BUFFER_TOO_SMALL, "GET_GUID ioctl with NULL buffer");
  EXPECT_RC(ctx->device->ops->ioctl(ctx->device, IOCTL_BLOCK_GET_GUID, NULL, 0,
                                    &guid, sizeof(guid) - 1),
            ERR_BUFFER_TOO_SMALL, "GET_GUID ioctl with small buffer");
  EXPECT_GT(ctx->device->ops->ioctl(ctx->device, IOCTL_BLOCK_GET_GUID, NULL, 0,
                                    &guid, sizeof(guid)),
            0, "GET_GUID ioctl");
  EXPECT_EQ(guid, kGuid, "wrong guid");
  // Test IOCTLs that aren't handled.
  EXPECT_RC(ctx->device->ops->ioctl(ctx->device, IOCTL_BLOCK_RR_PART, NULL, 0,
                                    &size, sizeof(size)),
            ERR_NOT_SUPPORTED, "unsupported ioctl");
  MXDM_END_TEST(ctx);
}

// test_callbacks
static mxdm_txn_action_t test_callbacks_impl(mxdm_worker_t *worker,
                                             iotxn_t *txn, uint64_t *blkoff,
                                             uint64_t blkmax, bool is_before,
                                             bool is_read) {
  EXPECT_NEQ(worker, NULL, "worker");
  EXPECT_NEQ(txn, NULL, "txn");
  EXPECT_NEQ(blkoff, NULL, "blkoff");
  if (!worker || !txn || !blkoff) {
    txn->status = ERR_INTERNAL;
    return kMxdmCompleteTxn;
  }
  if (is_read) {
    EXPECT_EQ(txn->opcode, (uint32_t)IOTXN_OP_READ, "txn->opcode");
  } else {
    EXPECT_EQ(txn->opcode, (uint32_t)IOTXN_OP_WRITE, "txn->opcode");
  }
  EXPECT_RC(txn->status, NO_ERROR, "txn->status");
  if (is_before) {
    EXPECT_EQ(txn->actual, 0U, "txn->actual pre I/O");
  } else {
    EXPECT_EQ(txn->actual, txn->length, "txn->actual post I/O");
  }
  EXPECT_EQ(blkmax - *blkoff, txn->length / MXDM_BLOCK_SIZE,
            "txn->length vs. blklen");
  *blkoff = blkmax;
  return (is_before ? kMxdmContinueTxn : kMxdmCompleteTxn);
}

static mxdm_txn_action_t test_callbacks_before_read(mxdm_worker_t *worker,
                                                    iotxn_t *txn,
                                                    uint64_t *blkoff,
                                                    uint64_t blkmax) {
  return test_callbacks_impl(worker, txn, blkoff, blkmax, true /* before */,
                             true /* read */);
}

static mxdm_txn_action_t test_callbacks_before_write(mxdm_worker_t *worker,
                                                     iotxn_t *txn,
                                                     uint64_t *blkoff,
                                                     uint64_t blkmax) {
  return test_callbacks_impl(worker, txn, blkoff, blkmax, true /* before */,
                             false /* write */);
}

static mxdm_txn_action_t test_callbacks_after_read(mxdm_worker_t *worker,
                                                   iotxn_t *txn,
                                                   uint64_t *blkoff,
                                                   uint64_t blkmax) {
  return test_callbacks_impl(worker, txn, blkoff, blkmax, false /* after */,
                             true /* read */);
}

static mxdm_txn_action_t test_callbacks_after_write(mxdm_worker_t *worker,
                                                    iotxn_t *txn,
                                                    uint64_t *blkoff,
                                                    uint64_t blkmax) {
  return test_callbacks_impl(worker, txn, blkoff, blkmax, false /* after */,
                             false /* write */);
}

static bool test_callbacks(void) {
  mxdm_test_ctx_t *ctx = mxdm_test_ctx_init();
  ctx->worker_ops.before_read = test_callbacks_before_read;
  ctx->worker_ops.before_write = test_callbacks_before_write;
  ctx->worker_ops.after_read = test_callbacks_after_read;
  ctx->worker_ops.after_write = test_callbacks_after_write;
  MXDM_BEGIN_TEST(ctx);
  completion_t completion1 = COMPLETION_INIT;
  completion_t completion2 = COMPLETION_INIT;
  EXPECT_TRUE(mxdm_test_queue_iotxn(ctx, IOTXN_OP_READ, &completion1),
              "mxdm_test_queue_iotxn");
  EXPECT_TRUE(mxdm_test_queue_iotxn(ctx, IOTXN_OP_WRITE, &completion2),
              "mxdm_test_queue_iotxn");
  EXPECT_RC(completion_wait(&completion2, kTimeout), NO_ERROR,
            "completion_wait");
  EXPECT_RC(completion_wait(&completion1, kTimeout), NO_ERROR,
            "completion_wait");
  MXDM_END_TEST(ctx);
}

// test_sync_io
static mx_status_t test_sync_io_prepare(mxdm_worker_t *worker, uint64_t blklen,
                                        uint64_t *data_blkoff,
                                        uint64_t *data_blklen) {
  const mxdm_device_t *device = mxdm_worker_get_device(worker);
  uint8_t buf[MXDM_BLOCK_SIZE + 1];
  // Test mxdm_read_block
  EXPECT_RC(mxdm_read_block(device, 0, buf, MXDM_BLOCK_SIZE), NO_ERROR,
            "mxdm_read_block");
  EXPECT_RC(mxdm_read_block(NULL, 0, buf, MXDM_BLOCK_SIZE), ERR_INVALID_ARGS,
            "mxdm_read_block with NULL mxdm");
  EXPECT_RC(mxdm_read_block(device, MXDM_TEST_BLOCKS, buf, MXDM_BLOCK_SIZE),
            ERR_INVALID_ARGS, "mxdm_read_block with OOB block");
  EXPECT_RC(mxdm_read_block(device, 0, NULL, MXDM_BLOCK_SIZE), ERR_INVALID_ARGS,
            "mxdm_read_block with bad buffer");
  EXPECT_RC(mxdm_read_block(device, 0, buf, 0), ERR_INVALID_ARGS,
            "mxdm_read_block with short read");
  EXPECT_RC(mxdm_read_block(device, 0, buf, MXDM_BLOCK_SIZE + 1),
            ERR_INVALID_ARGS, "mxdm_read_block with long read");
  // Test mxdm_write_block
  EXPECT_RC(mxdm_write_block(device, 0, buf, MXDM_BLOCK_SIZE), NO_ERROR,
            "mxdm_write_block");
  EXPECT_RC(mxdm_write_block(NULL, 0, buf, MXDM_BLOCK_SIZE), ERR_INVALID_ARGS,
            "mxdm_write_block with NULL mxdm");
  EXPECT_RC(mxdm_write_block(device, MXDM_TEST_BLOCKS, buf, MXDM_BLOCK_SIZE),
            ERR_INVALID_ARGS, "mxdm_write_block with OOB block");
  EXPECT_RC(mxdm_write_block(device, 0, NULL, MXDM_BLOCK_SIZE),
            ERR_INVALID_ARGS, "mxdm_write_block with bad buffer");
  EXPECT_RC(mxdm_write_block(device, 0, buf, 0), ERR_INVALID_ARGS,
            "mxdm_write_block with short write");
  EXPECT_RC(mxdm_write_block(device, 0, buf, MXDM_BLOCK_SIZE + 1),
            ERR_INVALID_ARGS, "mxdm_write_block with long write");
  return mxdm_test_prepare(worker, blklen, data_blkoff, data_blklen);
}

static bool test_sync_io(void) {
  mxdm_test_ctx_t *ctx = mxdm_test_ctx_init();
  ctx->worker_ops.prepare = test_sync_io_prepare;
  MXDM_BEGIN_TEST(ctx);
  MXDM_END_TEST(ctx);
}

// test_block_cache
static mxdm_txn_action_t test_block_cache_before_read(mxdm_worker_t *worker,
                                                      iotxn_t *txn,
                                                      uint64_t *blkoff,
                                                      uint64_t blkmax) {
  if (*blkoff == 0 && blkmax == 1) {
    *blkoff = blkmax;
    return kMxdmContinueTxn;
  }
  // Test is_data
  EXPECT_FALSE(mxdm_is_data_block(NULL, 1), "mxdm_is_data with NULL worker");
  EXPECT_FALSE(mxdm_is_data_block(worker, MXDM_TEST_BLOCKS),
               "mxdm_is_data with OOB block");
  EXPECT_FALSE(mxdm_is_data_block(worker, 0),
               "mxdm_is_data with non-data block");
  EXPECT_TRUE(mxdm_is_data_block(worker, 1), "mxdm_is_data with data block");

  // Test acquire_block
  mxdm_block_t *block = NULL;
  EXPECT_RC(mxdm_acquire_block(NULL, 0, &block), ERR_INVALID_ARGS,
            "mxdm_acquire_block with NULL worker");
  EXPECT_RC(mxdm_acquire_block(worker, 0, NULL), ERR_INVALID_ARGS,
            "mxdm_acquire_block with NULL block");
  EXPECT_RC(mxdm_acquire_block(worker, 0, &block), NO_ERROR,
            "mxdm_acquire_block");
  // Test is_ready
  EXPECT_FALSE(mxdm_block_is_ready(NULL),
               "mxdm_block_is_ready with NULL block");
  if (!mxdm_block_is_ready(block)) {
    // Test wait_for_block
    mxdm_wait_for_block(NULL, txn);
    mxdm_wait_for_block(block, NULL);
    mxdm_wait_for_block(block, txn);
    // Poke the test thread.
    mxdm_test_sync_wake();
    return kMxdmIgnoreTxn;
  }
  // Test get_block
  const char *hello = "hello world";
  char buffer[12] = {0};
  mxdm_get_block(NULL, MXDM_BLOCK_SIZE / 3, 12, buffer);
  mxdm_get_block(block, MXDM_BLOCK_SIZE / 3, 12, NULL);
  mxdm_get_block(block, MXDM_BLOCK_SIZE / 3, 12, buffer);
  // Test put_block
  mxdm_put_block(NULL, MXDM_BLOCK_SIZE / 3, 12, block);
  mxdm_put_block(hello, MXDM_BLOCK_SIZE / 3, 12, NULL);
  mxdm_put_block(hello, MXDM_BLOCK_SIZE / 3, 12, block);
  mxdm_get_block(block, MXDM_BLOCK_SIZE / 3, 12, buffer);
  EXPECT_EQ(strncmp(buffer, hello, 12), 0, "wrong buffer");
  // Test release_block
  mxdm_release_block(NULL, block);
  mxdm_release_block(worker, NULL);
  mxdm_release_block(worker, block);
  *blkoff = blkmax;
  return kMxdmContinueTxn;
}

static bool test_block_cache(void) {
  mxdm_test_ctx_t *ctx = mxdm_test_ctx_init();
  ctx->worker_ops.before_read = test_block_cache_before_read;
  MXDM_BEGIN_TEST(ctx);
  ctx->delay = true;
  completion_t completion = COMPLETION_INIT;
  EXPECT_TRUE(mxdm_test_queue_iotxn(ctx, IOTXN_OP_READ, &completion),
              "mxdm_test_queue_iotxn");
  mxdm_test_sync_wait();
  ctx->delay = false;
  iotxn_t *prev = NULL;
  iotxn_t *temp = NULL;
  list_for_every_entry_safe(&ctx->txns, prev, temp, iotxn_t, node) {
    list_delete(&prev->node);
    prev->ops->complete(prev, prev->status, prev->actual);
  }
  completion_wait(&completion, kTimeout);
  MXDM_END_TEST(ctx);
}

// test_block_bitmap
static mx_status_t test_block_bitmap_prepare(mxdm_worker_t *worker,
                                             uint64_t blklen,
                                             uint64_t *data_blkoff,
                                             uint64_t *data_blklen) {
  // Test check_block
  EXPECT_FALSE(mxdm_check_block(NULL, 0), "mxdm_check_block with NULL worker");
  EXPECT_FALSE(mxdm_check_block(worker, MXDM_TEST_BLOCKS),
               "mxdm_check_block with OOB block");
  EXPECT_FALSE(mxdm_check_block(worker, 0), "mxdm_check_block");
  // Test mark_block
  EXPECT_RC(mxdm_mark_block(NULL, 0), ERR_INVALID_ARGS,
            "mxdm_mark_block with NULL worker");
  EXPECT_RC(mxdm_mark_block(worker, MXDM_TEST_BLOCKS), ERR_INVALID_ARGS,
            "mxdm_mark_block with OOB block");
  EXPECT_RC(mxdm_mark_block(worker, 0), NO_ERROR, "mxdm_mark_block");
  EXPECT_TRUE(mxdm_check_block(worker, 0), "mxdm_check_block");
  // Test check_blocks
  uint64_t begin = 0;
  uint64_t end = 4;
  EXPECT_FALSE(mxdm_check_blocks(NULL, &begin, end),
               "mxdm_check_blocks with NULL worker");
  EXPECT_FALSE(mxdm_check_blocks(worker, NULL, end),
               "mxdm_check_blocks with NULL worker");
  EXPECT_TRUE(mxdm_check_blocks(worker, &begin, 0),
              "mxdm_check_blocks with empty range");
  EXPECT_EQ(begin, 0U, "before any mxdm_check_blocks");
  EXPECT_FALSE(mxdm_check_blocks(worker, &begin, end), "mxdm_check_blocks");
  EXPECT_EQ(begin, 1U, "after one mxdm_check_blocks");
  begin = 0;
  EXPECT_FALSE(mxdm_check_blocks(worker, &begin, MXDM_TEST_BLOCKS + 1),
               "mxdm_check_blocks with OOB block");
  EXPECT_EQ(begin, 1U, "after OOB mxdm_check_blocks");
  for (size_t i = begin; i < end; ++i) {
    EXPECT_RC(mxdm_mark_block(worker, i), NO_ERROR, "mxdm_mark_block");
  }
  EXPECT_TRUE(mxdm_check_blocks(worker, &begin, end), "mxdm_check_blocks");
  EXPECT_EQ(begin, end, "after all mxdm_check_blocks");
  // Test clear_blocks
  EXPECT_RC(mxdm_clear_blocks(NULL, 1, 2), ERR_INVALID_ARGS,
            "mxdm_clear_blocks with NULL worker");
  EXPECT_RC(mxdm_clear_blocks(worker, 2, 1), NO_ERROR,
            "mxdm_clear_blocks with bad range");
  EXPECT_RC(mxdm_clear_blocks(worker, 0, 0), NO_ERROR,
            "mxdm_clear_blocks with empty range");
  EXPECT_RC(mxdm_clear_blocks(worker, 0, MXDM_TEST_BLOCKS + 1), NO_ERROR,
            "mxdm_clear_blocks with OOB block");
  begin = 0;
  EXPECT_FALSE(mxdm_check_blocks(worker, &begin, end), "mxdm_check_blocks");
  EXPECT_EQ(begin, 0U, "after cleared mxdm_check_blocks");
  // Test block conversion and compression
  for (size_t i = 0; i < MXDM_TEST_BLOCKS; i += 2) {
    EXPECT_RC(mxdm_mark_block(worker, i), NO_ERROR, "mxdm_mark_block");
  }
  for (size_t i = 0; i < MXDM_TEST_BLOCKS; i += 2) {
    EXPECT_TRUE(mxdm_check_block(worker, i), "mxdm_check_block");
  }
  for (size_t i = 1; i < MXDM_TEST_BLOCKS; i += 2) {
    EXPECT_FALSE(mxdm_check_block(worker, i), "mxdm_check_block");
  }
  EXPECT_RC(mxdm_clear_blocks(worker, 0, MXDM_TEST_BLOCKS), NO_ERROR,
            "mxdm_clear_blocks");
  return mxdm_test_prepare(worker, blklen, data_blkoff, data_blklen);
}

static bool test_block_bitmap(void) {
  mxdm_test_ctx_t *ctx = mxdm_test_ctx_init();
  ctx->worker_ops.prepare = test_block_bitmap_prepare;
  MXDM_BEGIN_TEST(ctx);
  MXDM_END_TEST(ctx);
}
#undef MXDM_BEGIN_TEST
#undef MXDM_END_TEST

// Bind subroutines
BEGIN_TEST_CASE(mxdm_tests)
RUN_TEST(test_get_size)
RUN_TEST(test_ioctl)
RUN_TEST(test_callbacks)
RUN_TEST(test_sync_io)
RUN_TEST(test_block_cache)
RUN_TEST(test_block_bitmap)
END_TEST_CASE(mxdm_tests)

int main(int argc, char **argv) {
  printf("hello world!\n");
  return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
