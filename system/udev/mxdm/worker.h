// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines protected woker functions available to other components,
// such as the device's devmgr callbacks. This header should NOT be included by
// drivers; see the comments in common.h for additional info.

#pragma once

#ifndef MXDM_IMPLEMENTATION
#error "This file should only be included by the MXDM framework."
#endif

#include "mxdm.h"

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/iotxn.h>
#include <magenta/types.h>

// Types

// Initialization info passed from the device to the worker thread.
typedef struct mxdm_init_info {
    // The specific MXDM driver binding to the device.
    mx_driver_t* drv;
    // Parent device in devmgr's device tree.
    mx_device_t* parent;
    // The MXDM device structure.
    mxdm_device_t* device;
    // The worker's callbacks.
    const mxdm_worker_ops_t* ops;
    // Name of the device
    char name[MX_DEVICE_NAME_MAX];
} mxdm_init_info_t;

// Functions

// The MXDM worker thread routine. 'arg' is an mxdm_init_info structure.
int mxdm_worker(void* arg);

// Returns the aggregate size of all the data blocks of a device.
mx_off_t mxdm_worker_data_size(mxdm_worker_t* worker);

// Configures I/O completion callbacks for both cloned and cached iotxns.
mx_status_t mxdm_worker_set_cb(mxdm_worker_t* worker, iotxn_t* txn,
                               void* origin);

// Attempts to add an I/O transaction to the worker's queue for processing.  If
// it fails (e.g. the worker is exiting), the transaction is passed to
// mxdm_complete_txn.
void mxdm_worker_queue(mxdm_worker_t* worker, iotxn_t* txn);

// Instructs the worker thread to stop processing I/O transactions.  The worker
// closes its queue to new requests and begins completing requests with an
// ERR_HANDLE_CLOSED status.
void mxdm_worker_stop(mxdm_worker_t* worker);

// This is called when all handles to the device have closed.  The worker thread
// asynchronously makes sure there are no remaining requests outstanding and
// then frees all it resources before terminating.
void mxdm_worker_exit(mxdm_worker_t* worker);
