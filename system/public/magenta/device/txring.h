// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

__BEGIN_CDECLS

#define MX_TXRING_SIGNAL_QUEUE      MX_USER_SIGNAL_0
#define MX_TXRING_SIGNAL_COMPLETE   MX_USER_SIGNAL_1

// mx_txring_entry_t flags
#define MX_TXRING_QUEUED    0x1     // transaction is queued, entry is owned by the consumer

// transaction ring entry definition
typedef struct {
    // size of data in buffer VMO for this transaction, or zero if none
    // written by producer, untouched by consumer
    uint32_t data_size;

    // offset of data in buffer VMO
    // written by producer, untouched by consumer
    uint32_t data_offset;

    // result code returned from driver upon transaction completion
    // written by consumer 
    mx_status_t status;

    // Flags indicating current state of the transaction
    // only writable by current owner
    uint32_t flags;

    // Private data written by producer, opaque and untouched by consumer
    uint64_t cookie;

    
    // device-specific protocol data
    // written by producer, untouched by consumer
    union {
        uint64_t proto_data_64;
        uint32_t proto_data_32[2];
        uint32_t proto_data_8[8];
    };
} mx_txring_entry_t;

__END_CDECLS
