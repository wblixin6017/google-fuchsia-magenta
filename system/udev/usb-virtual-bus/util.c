// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/hw/usb.h>
#include "util.h"

uint8_t ep_addr_to_index(uint8_t ep_address) {
    if (ep_address == 0) return 0;
    uint32_t index = 2 * (ep_address & ~USB_ENDPOINT_DIR_MASK);
    if ((ep_address & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_OUT)
        index--;
    return index;
}
