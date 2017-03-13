// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <ddk/device.h>
#include <ddk/iotxn.h>
#include <magenta/compiler.h>
#include <magenta/hw/usb.h>
#include <magenta/hw/usb-hub.h>

__BEGIN_CDECLS;

// opcodes for iotxn_t.opcode
#define USB_OPCODE_TXN       0   // for transactions on USB endpoints
#define USB_OPCODE_RESET_EP  1   // resets a halted endpoint

// protocol data for iotxns
typedef struct usb_protocol_data {
    usb_setup_t setup;      // for control transactions
    uint64_t frame;         // frame number for scheduling isochronous transfers
    uint32_t device_id;
    uint8_t ep_address;     // bEndpointAddress from endpoint descriptor
} usb_protocol_data_t;

static_assert(sizeof(usb_protocol_data_t) <= sizeof(iotxn_protocol_data_t), "");

__END_CDECLS;
