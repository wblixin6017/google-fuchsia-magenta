// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

// Bind to a driver
//   in: driver to bind to (optional)
//   out: none
#define IOCTL_DEVICE_BIND \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 0)

// Watch a directory for changes
//   in: none
//   out: handle to msgpipe to get notified on
#define IOCTL_DEVICE_WATCH_DIR \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DEVICE, 1)

// Return a handle to the device event
//   in: none
//   out: handle
#define IOCTL_DEVICE_GET_EVENT_HANDLE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DEVICE, 2)

// Return driver name string
//   in: none
//   out: null-terminated string
#define IOCTL_DEVICE_GET_DRIVER_NAME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 3)

// Return device name string
//   in: none
//   out: null-terminated string
#define IOCTL_DEVICE_GET_DEVICE_NAME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 4)

// Suspends the device
// (intended for driver suspend/resume testing)
//   in: none
//   out: none
#define IOCTL_DEVICE_DEBUG_SUSPEND \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 5)

// Resumes the device
// (intended for driver suspend/resume testing)
//   in: none
//   out: none
#define IOCTL_DEVICE_DEBUG_RESUME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 6)

// Sync the device
//   in: none
//   out: none
#define IOCTL_DEVICE_SYNC \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 7)

// Create a transaction ring for scheduling transactions with the device
//   in: index of txring to create (uint32_t)
//   in: shared buffer size (uint32_t)
//   in: entry count for transaction ring (uint32_t)
//   out: shared buffer VMO handle
//   out: transaction ring VMO handle
#define IOCTL_DEVICE_TXRING_CREATE \
    IOCTL(IOCTL_KIND_GET_TWO_HANDLES, IOCTL_FAMILY_DEVICE, 7)

#define IOCTL_DEVICE_TXRING_RELEASE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 8)

// Indicates if there's data available to read,
// or room to write, or an error condition.
#define DEVICE_SIGNAL_READABLE MX_USER_SIGNAL_0
#define DEVICE_SIGNAL_WRITABLE MX_USER_SIGNAL_1
#define DEVICE_SIGNAL_ERROR MX_USER_SIGNAL_2

// ssize_t ioctl_device_bind(int fd, const char* in, size_t in_len);
IOCTL_WRAPPER_VARIN(ioctl_device_bind, IOCTL_DEVICE_BIND, char);

// ssize_t ioctl_device_watch_dir(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_device_watch_dir, IOCTL_DEVICE_WATCH_DIR, mx_handle_t);

// ssize_t ioctl_device_get_event_handle(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_device_get_event_handle, IOCTL_DEVICE_GET_EVENT_HANDLE, mx_handle_t);

// ssize_t ioctl_device_get_driver_name(int fd, char* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_device_get_driver_name, IOCTL_DEVICE_GET_DRIVER_NAME, char);

// ssize_t ioctl_device_get_device_name(int fd, char* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_device_get_device_name, IOCTL_DEVICE_GET_DEVICE_NAME, char);

// ssize_t ioctl_device_debug_suspend(int fd);
IOCTL_WRAPPER(ioctl_device_debug_suspend, IOCTL_DEVICE_DEBUG_SUSPEND);

// ssize_t ioctl_device_debug_resume(int fd);
IOCTL_WRAPPER(ioctl_device_debug_resume, IOCTL_DEVICE_DEBUG_RESUME);

// ssize_t ioctl_device_sync(int fd);
IOCTL_WRAPPER(ioctl_device_sync, IOCTL_DEVICE_SYNC);

typedef struct {
    uint32_t index;
    uint32_t buf_size;
    uint32_t txring_count;
} mx_txring_create_in_args_t;

typedef struct {
    mx_handle_t buf_vmo;
    mx_handle_t txring_vmo;
} mx_txring_create_out_args_t;

static inline mx_status_t ioctl_device_txring_create(int fd, uint32_t index, uint32_t buf_size,
                                                     uint32_t txring_count,
                                                     mx_handle_t* out_buf_vmo,
                                                     mx_handle_t* out_txring_vmo) {
    mx_txring_create_in_args_t in_args;
    mx_txring_create_out_args_t out_args;
    in_args.index = index;
    in_args.buf_size = buf_size;
    in_args.txring_count = txring_count;

    ssize_t result = mxio_ioctl(fd, IOCTL_DEVICE_TXRING_CREATE, &in_args, sizeof(in_args),
                                &out_args, sizeof(out_args));

    if (result < 0) return result;
    *out_buf_vmo = out_args.buf_vmo;
    *out_txring_vmo = out_args.txring_vmo;
    return 0;
}

// ssize_t ioctl_device_txring_release(int fd, uint32_t index);
IOCTL_WRAPPER_IN(ioctl_device_txring_release, IOCTL_DEVICE_TXRING_RELEASE, uint32_t);
