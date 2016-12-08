// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/device/txring.h>

#include <stddef.h>
#include <stdbool.h>

__BEGIN_CDECLS

// Glue for handling ioctl_txring_create and ioctl_txring_release
// Simply add this macro inside the switch statement for your driver's ioctl handler
// For dev, pass a pointer to your mx_device_t, and for create and release,
// pass functions with these signatures:
//
//    mx_status_t my_txring_create(mx_device_t* dev, uint32_t index, uint32_t buf_size,
//                                 uint32_t txring_count, mx_handle_t* out_buf_vmo,
//                                 mx_handle_t* out_txring_vmo);
//
//    mx_status_t my_txring_release(mx_device_t* dev, uint32_t index);
//
// It is assumed your ioctl handler names its arguments in_len, in_buf, out_len and out_buf.
#define IOCTL_TXRING_GLUE(dev, create, release)                                     \
    case IOCTL_DEVICE_TXRING_CREATE: {                                              \
        if (in_len != 3 * sizeof(uint32_t) || out_len != 2 * sizeof(mx_handle_t)) { \
            return ERR_INVALID_ARGS;                                                \
        }                                                                           \
        mx_txring_create_in_args_t* in = (mx_txring_create_in_args_t *)in_buf;      \
        mx_txring_create_out_args_t* out = (mx_txring_create_out_args_t *)out_buf;  \
        return create(dev, in->index, in->buf_size, in->txring_count,               \
                      &out->buf_vmo, &out->txring_vmo);                             \
    }                                                                               \
    case IOCTL_DEVICE_TXRING_RELEASE: {                                             \
        if (in_len != sizeof(uint32_t) || out_len != 0) {                           \
            return ERR_INVALID_ARGS;                                                \
        }                                                                           \
        uint32_t* in = (uint32_t *)in_buf;                                          \
        return release(dev, in[0]);                                                 \
    }

typedef struct {
    mx_handle_t buffer_vmo;
    mx_handle_t txring_vmo;
    uint32_t buffer_size;
    uint32_t txring_count;

    // VMO mappings
    uint8_t* buffer;
    mx_txring_entry_t* ring;
} txring_t;

mx_status_t txring_init(txring_t* txring, uint32_t buf_size, uint32_t txring_count, bool contiguous);
void txring_release(txring_t* txring);

__END_CDECLS
