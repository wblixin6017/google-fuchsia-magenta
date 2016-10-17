// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides a dummy device that can be used as the underlying block
// device when testing MXDM.

#include "mxdm.h"
#include "private.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/iotxn.h>
#include <magenta/device/block.h>
#include <magenta/fuchsia-types.h>
#include <magenta/types.h>

// Forward declarations

// Queues an I/O transaction.  For testing it just completes it immediately, or
// saves it for later if ctx.delay is set.
static void mxdm_test_iotxn_queue(mx_device_t *dev, iotxn_t *txn);

// Called when an I/O transaction completes.  For testing this just cleans up
// the iotxn and signals any waiters.
static void mxdm_test_complete_cb(iotxn_t *txn, void *cookie);

// Returns the size of the device.  For testing this is MXDM_TEST_BLOCKS blocks.
static mx_off_t mxdm_test_get_size(mx_device_t *dev);

// Performs an I/O control.  For testing only IOCTL_BLOCK_GET_GUID and
// IOCTL_BLOCK_GET_BLOCKSIZE are supported.
static ssize_t mxdm_test_ioctl(mx_device_t *dev, uint32_t op,
                               const void *in_buf, size_t in_len, void *out_buf,
                               size_t out_len);

// Frees the associated resources.  For testing this is a no-op.
static mx_status_t mxdm_test_release(mx_device_t *dev);

// Variables

// Bundles the devmgr-facing functions into a protocol structure.
static mx_protocol_device_t mxdm_test_ops = {
    .iotxn_queue = mxdm_test_iotxn_queue,
    .get_size = mxdm_test_get_size,
    .ioctl = mxdm_test_ioctl,
    .release = mxdm_test_release,
};

// Public functions

void mxmd_test_init_parent(mx_device_t *parent, mxdm_test_ctx_t *ctx) {
  memset(parent, 0, sizeof(mx_device_t));
  parent->ops = &mxdm_test_ops;
  parent->ctx = ctx;
  snprintf(parent->name, MX_DEVICE_NAME_MAX, "parent");
}

bool mxdm_test_queue_iotxn(mxdm_test_ctx_t *ctx, uint32_t opcode,
                           completion_t *completion) {
  iotxn_t *txn = NULL;
  mx_status_t rc = iotxn_alloc(&txn, (MXDM_TEST_BLOCKS - 1) * MXDM_BLOCK_SIZE,
                               MXDM_BLOCK_SIZE, 0);
  EXPECT_RC(rc, NO_ERROR, "iotxn_alloc");
  if (rc != NO_ERROR) {
    return false;
  }
  txn->opcode = opcode;
  txn->offset = 0;
  txn->length = 4 * MXDM_BLOCK_SIZE;
  txn->complete_cb = mxdm_test_complete_cb;
  txn->cookie = completion;
  mx_device_t *dev = ctx->device;
  dev->ops->iotxn_queue(dev, txn);
  return true;
}

// Private functions

static void mxdm_test_iotxn_queue(mx_device_t *dev, iotxn_t *txn) {
  txn->status = (txn->flags == 0 ? NO_ERROR : ERR_IO);
  txn->actual = (txn->status == NO_ERROR ? txn->length : 0);
  EXPECT_FALSE(list_in_list(&txn->node), "txn should not be in any list");
  if (list_in_list(&txn->node)) {
    list_delete(&txn->node);
  }
  mxdm_test_ctx_t *ctx = dev->ctx;
  if (ctx->delay) {
    list_add_tail(&ctx->txns, &txn->node);
  } else {
    txn->ops->complete(txn, txn->status, txn->actual);
  }
}

static void mxdm_test_complete_cb(iotxn_t *txn, void *cookie) {
  if (cookie) {
    completion_t *completion = cookie;
    completion_signal(completion);
  }
  txn->ops->release(txn);
}

static mx_off_t mxdm_test_get_size(mx_device_t *dev) {
  return MXDM_TEST_BLOCKS * MXDM_BLOCK_SIZE;
}

static ssize_t mxdm_test_ioctl(mx_device_t *dev, uint32_t op,
                               const void *in_buf, size_t in_len, void *out_buf,
                               size_t out_len) {
  mxdm_test_ctx_t *ctx = dev->ctx;
  switch (op) {
    case IOCTL_BLOCK_GET_GUID:
      if (!out_buf || out_len < sizeof(ctx->guid)) {
        return ERR_BUFFER_TOO_SMALL;
      }
      uint64_t *guid = out_buf;
      *guid = ctx->guid;
      return sizeof(ctx->guid);
    case IOCTL_BLOCK_GET_BLOCKSIZE:
      if (!out_buf || out_len < sizeof(size_t)) {
        return ERR_BUFFER_TOO_SMALL;
      }
      size_t *size = out_buf;
      *size = MXDM_BLOCK_SIZE;
      return sizeof(size_t);
    default:
      return ERR_NOT_SUPPORTED;
  }
}

static mx_status_t mxdm_test_release(mx_device_t *dev) { return NO_ERROR; }
