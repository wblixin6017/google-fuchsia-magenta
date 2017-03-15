// Copyright 2017 The Fuchsia Authors. All riusghts reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>
#include <threads.h>

#include "usb-virtual-bus.h"


static mtx_t lock = MTX_INIT;
static mx_handle_t channel_handles[2] = { MX_HANDLE_INVALID, MX_HANDLE_INVALID};

enum {
    HOST_CHANNEL,
    CLIENT_CHANNEL
};

static mx_handle_t usb_virt_get_channel(int channel) {
    mtx_lock(&lock);
    if (channel_handles[channel] == MX_HANDLE_INVALID) {
        mx_channel_create(0, &channel_handles[0], &channel_handles[1]);
    }
    mtx_unlock(&lock);
    return channel_handles[channel];
}

mx_handle_t usb_virt_get_host_channel(void) {
    return usb_virt_get_channel(HOST_CHANNEL);
}

mx_handle_t usb_virt_get_client_channel(void) {
    return usb_virt_get_channel(CLIENT_CHANNEL);
}
