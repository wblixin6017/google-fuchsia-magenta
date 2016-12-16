// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/device/audio.h>
#include <threads.h>

typedef mx_status_t (* txring_callback)(void* data, size_t length, void* cookie);

typedef struct {
    mx_handle_t buffer_vmo;
    mx_handle_t txring_vmo;
    mx_handle_t fifo_handle;
    mx_handle_t stop_event;
    uint64_t buffer_size;
    uint32_t txring_count;

    // VMO mappings
    uint8_t* buffer;
    mx_audio_txring_entry_t* ring;

    // current index in the txring
    uint32_t txring_index;

    // current fifo state
    mx_fifo_state_t fifo_state;

    thrd_t thread;
    txring_callback callback;
    void* cookie;
} usb_audio_txring_t;

mx_status_t usb_audio_txring_init(usb_audio_txring_t* txring);

// helper for IOCTL_AUDIO_SET_BUFFER, IOCTL_AUDIO_SET_TXRING and IOCTL_AUDIO_GET_FIFO
mx_status_t usb_audio_txring_ioctl(usb_audio_txring_t* txring, uint32_t op, const void* in_buf,
                                   size_t in_len, void* out_buf, size_t out_len);

void usb_audio_txring_start(usb_audio_txring_t* txring, txring_callback callback, void* cookie);
void usb_audio_txring_stop(usb_audio_txring_t* txring);

void usb_audio_txring_release(usb_audio_txring_t* txring);
