// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <magenta/compiler.h>
#include <magenta/hw/usb.h>
#include <stdbool.h>

__BEGIN_CDECLS;

// callbacks installed by function driver
typedef struct usb_client_callbacks {
    // callback for handling ep0 control requests
    mx_status_t (*control)(const usb_setup_t* setup, void* buffer, int length, void* cookie);
} usb_client_callbacks_t;

typedef struct usb_client_protocol {
    void (*set_callbacks)(mx_device_t* dev, usb_client_callbacks_t* callbacks, void* cookie);
    mx_status_t (*config_ep)(mx_device_t* dev, const usb_endpoint_descriptor_t* ep_desc);
} usb_client_protocol_t;

__END_CDECLS;
