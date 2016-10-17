// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines the protected device functions available to other
// components, namely, the worker thread.  This header should NOT be included by
// drivers; see the comments in common.h for additional info.

#pragma once

#ifndef MXDM_IMPLEMENTATION
#error "This file should only be included by the MXDM framework."
#endif

#include <ddk/iotxn.h>
#include <magenta/types.h>

#include "mxdm.h"
#include "worker.h"

// Functions

// Uses the given info to added the device to devmgr.
mx_status_t mxdm_device_init(mxdm_worker_t* worker, mxdm_init_info_t* info);

// Frees any memory and/or resources associated with 'mxdm'.  This should only
// be called by the worker during clean-up, or by mxdm_init on a pre-worker
// fatal error.
void mxdm_device_free(mxdm_device_t* device);

// Queues an I/O transaction to be sent to the underlying block device.
void mxdm_device_queue(mxdm_device_t* device, iotxn_t* txn);
