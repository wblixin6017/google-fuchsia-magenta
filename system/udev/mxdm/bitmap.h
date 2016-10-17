// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines the protected bitmap functions available to the worker 
// thread.  This header should NOT be included by drivers; see the comments in
// common.h for additional info.

#pragma once

#ifndef MXDM_IMPLEMENTATION
#error "This file should only be included by the MXDM framework."
#endif

#include <stdint.h>

#include <magenta/types.h>

#include "mxdm.h"

// Types

// A bitmap used by the worker to mark and clear blocks.
typedef struct mxdm_bitmap mxdm_bitmap_t;

// Functions

// Creates a new bitmap that can hold 'blklen' bits.
mx_status_t mxdm_bitmap_init(uint64_t bitlen, mxdm_bitmap_t** out);

// Releases memory associated with a bitmap.
void mxdm_bitmap_free(mxdm_bitmap_t* bitmap);

// Returns true if a all the range of bits form 'bitoff' to 'bitmax' are set.
// Otherwise, it sets 'bitoff' to the first bit that is not set and returns
// false.
bool mxdm_bitmap_get(mxdm_bitmap_t* bitmap, uint64_t* bitoff, uint64_t bitmax);

// Sets the bit given by 'bitoff' in the 'bitmap'.
mx_status_t mxdm_bitmap_set(mxdm_bitmap_t* bitmap, uint64_t bitoff);

// Clears the bit given by 'bitoff' in 'bitmap'.
mx_status_t mxdm_bitmap_clr(mxdm_bitmap_t* bitmap, uint64_t bitoff,
                            uint64_t bitmax);

// Converts any raw chunks that would use less memory as RLE chunks to RLE
// chunks.
void mxdm_bitmap_compress(mxdm_bitmap_t* bitmap);
