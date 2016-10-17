// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines the helper functions used when testing MXDM.

#include "mxdm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/iotxn.h>
#include <magenta/listnode.h>
#include <magenta/types.h>
#include <unittest/unittest.h>

// Macros

#define EXPECT_RC(expr, rc, msg)           \
  do {                                     \
    mx_status_t _rc = (mx_status_t)(expr); \
    EXPECT_EQ(_rc, rc, msg);               \
    mxdm_test_compare_rc(_rc, rc);         \
  } while (0)

// Constants

// The size of the fake device, in blocks.
#define MXDM_TEST_BLOCKS 131072

// Test state used by the fake device.
typedef struct mxdm_test_ctx {
  // Not used in testing, but needed by devmgr's interface
  mx_driver_t driver;
  // The fake device
  mx_device_t parent;
  // A handle to the devmgr device.
  mx_device_t *device;
  // Holds the device callbacks
  mxdm_device_ops_t device_ops;
  // Holds the worker callbacks
  mxdm_worker_ops_t worker_ops;
  // Set in the fake devmgr's device_add and cleared by its driver_unbind, this
  // indicates if the initialization completed successfully.  Needed as the
  // worker does initialization asynchronously.
  bool bound;
  // A value to respond to GUID I/O controls with.
  uint64_t guid;
  // If true, I/O transactions will be saved instead of completed.
  bool delay;
  // Used to store the saved I/O transactions.  If a routine clears ctx.delay,
  // it should re-queue these iotxns.
  list_node_t txns;
} mxdm_test_ctx_t;

// Functions

// Sets up a fake parent device for testing.
void mxmd_test_init_parent(mx_device_t *parent, mxdm_test_ctx_t *ctx);

// Queues an I/O transaction to the MXDM device.
bool mxdm_test_queue_iotxn(mxdm_test_ctx_t *ctx, uint32_t opcode,
                           completion_t *completion);

// Sets up the synchronization control structures.  Must be called before
// mxdm_sync_wait or mxdm_sync_wake.
bool mxdm_test_sync_init(void);

// Wait for another thread to tell the caller to continue.
void mxdm_test_sync_wait(void);

// Tells the thread waiting in mxdm_sync_wait to continue.
void mxdm_test_sync_wake(void);

// Check the return codes against each other.
void mxdm_test_compare_rc(mx_status_t actual, mx_status_t expected);
