// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdint.h>
#include <magenta/types.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

__BEGIN_CDECLS

enum {
    // Device type for MIDI source
    AUDIO_TYPE_SOURCE = 1,
    // Device type for MIDI sink
    AUDIO_TYPE_SINK = 2,
};

// struct representing an entry in txring VMO
typedef struct {
    // size of data in buffer VMO for this transaction, or zero if none
    // written by producer, untouched by consumer
    uint32_t data_size;

    // offset of data in buffer VMO
    // written by producer, untouched by consumer
    uint32_t data_offset;

    // Private data written by producer, opaque and untouched by consumer
    uint64_t cookie;

    // result code returned from driver upon transaction completion
    // written by consumer 
    mx_status_t status;

    // unused, set to zero
    uint32_t reserved;
} mx_audio_txring_entry_t;

// returns the device type (either AUDIO_TYPE_SOURCE or AUDIO_TYPE_SINK)
// call with out_len = sizeof(int)
#define IOCTL_AUDIO_GET_DEVICE_TYPE         IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 0)

// returns the number of supported sample rates
// call with out_len = sizeof(int)
#define IOCTL_AUDIO_GET_SAMPLE_RATE_COUNT   IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 1)

// returns the list of supported sample rates
// call with out_buf pointing to array of uint32_t and
// out_len = <value returned from IOCTL_AUDIO_GET_SAMPLE_RATE_COUNT> * sizeof(uint32_t)
#define IOCTL_AUDIO_GET_SAMPLE_RATES        IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 2)

// gets the current sample rate
// call with out_len = sizeof(uint32_t)
#define IOCTL_AUDIO_GET_SAMPLE_RATE         IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 3)

// sets the current sample rate
// call with in_len = sizeof(uint32_t)
#define IOCTL_AUDIO_SET_SAMPLE_RATE         IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 4)

// starts reading or writing audio data
// called with no arguments
#define IOCTL_AUDIO_START                   IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 5)

// stops reading or writing audio data
// called with no arguments
#define IOCTL_AUDIO_STOP                    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 7)

// sets buffer VMO to use for shared memory transactions
// buffer VMO can only be set when audio is stopped 
// called in_buf = buffer VMO handle, in_len = sizeof(mx_handle_t)
#define IOCTL_AUDIO_SET_BUFFER              IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_AUDIO, 8)

typedef struct {
    mx_handle_t txring;
    uint32_t count;
} mx_audio_set_txring_args_t;

// sets txring VMO to use for shared memory transactions
// the VMO will contain an array of mx_audio_txring_entry_t
// txring VMO can only be set when audio is stopped 
// called in_buf = mx_audio_set_txring_args_t, in_len = sizeof(mx_audio_set_txring_args_t)
#define IOCTL_AUDIO_SET_TXRING              IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_AUDIO, 9)

// returns a handle to a fifo to be used for scheduling shared memory transactions
// call with out_len = sizeof(mx_handle_t)
#define IOCTL_AUDIO_GET_FIFO                IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_AUDIO, 10)

IOCTL_WRAPPER_OUT(ioctl_audio_get_device_type, IOCTL_AUDIO_GET_DEVICE_TYPE, int);
IOCTL_WRAPPER_OUT(ioctl_audio_get_sample_rate_count, IOCTL_AUDIO_GET_SAMPLE_RATE_COUNT, int);
IOCTL_WRAPPER_VAROUT(ioctl_audio_get_sample_rates, IOCTL_AUDIO_GET_SAMPLE_RATES, uint32_t);
IOCTL_WRAPPER_OUT(ioctl_audio_get_sample_rate, IOCTL_AUDIO_GET_SAMPLE_RATE, uint32_t);
IOCTL_WRAPPER_IN(ioctl_audio_set_sample_rate, IOCTL_AUDIO_SET_SAMPLE_RATE, uint32_t);
IOCTL_WRAPPER(ioctl_audio_start, IOCTL_AUDIO_START);
IOCTL_WRAPPER(ioctl_audio_stop, IOCTL_AUDIO_STOP);
IOCTL_WRAPPER_IN(ioctl_audio_set_buffer, IOCTL_AUDIO_SET_BUFFER, mx_handle_t);
IOCTL_WRAPPER_IN(ioctl_audio_set_txring, IOCTL_AUDIO_SET_TXRING, mx_audio_set_txring_args_t);
IOCTL_WRAPPER_OUT(ioctl_audio_get_fifo, IOCTL_AUDIO_GET_FIFO, mx_handle_t);

__END_CDECLS
