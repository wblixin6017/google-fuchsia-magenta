// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>


typedef enum {
    // Sent from client to host to simulate device connect/disconnect
    USB_VIRT_CONNECT,
    USB_VIRT_DISCONNECT,
    // Sent from either side to simulate packet send
    USB_VIRT_PACKET,

} usb_virt_channel_cmd_t;

#define USB_VIRT_MAX_PACKET     (65536)
#define USB_VIRT_BUFFER_SIZE    (USB_VIRT_MAX_PACKET + sizeof(usb_virt_channel_cmd_t))

typedef struct {
    usb_virt_channel_cmd_t  cmd;
    // endpoint address for USB_VIRT_PACKET
    uint8_t                 ep_addr;
} usb_virt_header_t;

mx_handle_t usb_virt_get_host_channel(void);
mx_handle_t usb_virt_get_client_channel(void);
