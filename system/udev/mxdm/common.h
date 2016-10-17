// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Except for mxdm.h, all of the header files in this directory define the 
// "protected" interface of the MXDM block device filter driver framework.  
// These headers should NOT be included by implementing filter drivers.  Drivers
// should only include mxmd.h, which contains the "public" interface.
//
// The framework is organized into 4 parts:
// 1. bitmap.c provides a hybrid raw/run-length-encoded bitmap for tracking
//    blocks.
// 2. cache.c provides a block cache for recently accessed meta-data blocks,
//    that is, blocks used to store driver data and not consumer data.
// 3. device.c has the devmgr-related callbacks and functions for interacting
//    with the underlying block device.
// 4. worker.c provides the worker thread, which asynchonously implements all
//    the non-trivial device functions, including device initialization, I/O
//    transaction processing, and framework teardown.
//
// The primary "currency" of any device are I/O transactions, and internally
// MXDM differenitates between three types:
// 1. External transactions come in through iotxn_queue, which sets the context
//    field to inform the worker that it needs to clone the transaction.
// 2. Cloned transactions are created from external transactions in
//    mxdm_clone_txn. Cloning allow the worker to modify fields before passing
//    the transaction along. Clones are distinguished by having a NULL context
//    and an offset in the device's data region.
// 3. Cache transactions are created by metadata cache misses in
//    mxdm_acquire_block.  They are distinguished by having a NULL context and
//    an offset outside the device's data region.
//
// TODO(aarongreen): Currently, most of the constants have been pulled out of
// thin air as numbers that "seemed about right".  There needs to be an effort
// to properly measure performance and investigate what values or range of
// values should be used.

#pragma once

#ifndef MXDM_IMPLEMENTATION
#error "This file should only be included by the MXDM framework."
#endif

#include "mxdm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/iotxn.h>
#include <magenta/types.h>

// Macros

//#define TRACE
#ifdef TRACE
static mtx_t mxdm_trace_mtx;
// Allows multi-threaded tracing
#define MXDM_TRACE_INIT()                                     \
  if (mtx_init(&mxdm_trace_mtx, mtx_plain) != thrd_success) { \
    abort();                                                  \
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

// Public functions use the following to check passed pointers; protected and
// private functions simply assert.
#define MXDM_IF_NULL(arg, expr)       \
  if (!arg) {                         \
    MXDM_TRACE("'%s' is NULL", #arg); \
    expr;                             \
  }

// Types

