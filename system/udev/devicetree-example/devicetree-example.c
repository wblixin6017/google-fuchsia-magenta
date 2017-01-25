// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/devicetree.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static mx_status_t devicetree_example_bind(mx_driver_t* drv, mx_device_t* dev) {
    // TODO There should be a way to bind to devicetree devices in the binding stage.
    // Perhaps we can introduce vid/pid/did properties for each device tree entry instead of
    // strings.
    mx_protocol_devicetree_t* dt;
    if (device_get_protocol(dev, MX_PROTOCOL_DEVICETREE, (void**)&dt)) {
        return ERR_NOT_SUPPORTED;
    }

    if (!dt->is_compatible(dev, "qcom,msm-uartdm")) {
        return ERR_NOT_SUPPORTED;
    }

    printf("devicetree-example: matched with device=%p(%s)", dev, dev->name);
    return NO_ERROR;
}

mx_driver_t _driver_devicetree_example = {
    .ops = {
        .bind = devicetree_example_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_devicetree_example, "devicetree-example", "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_DEVICETREE),
MAGENTA_DRIVER_END(_driver_devicetree_example)
