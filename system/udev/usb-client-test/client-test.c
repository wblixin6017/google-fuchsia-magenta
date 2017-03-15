// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb-client.h>

typedef struct {
    mx_device_t device;
    mx_device_t* client_device;
    usb_client_protocol_t* client_protocol;

} usb_client_test_t;
#define get_usb_client_test(dev) containerof(dev, usb_client_test_t, device)

static mx_status_t client_test_control(const usb_setup_t* setup, void* buffer, int length, void* cookie) {
    printf("client_test_control type: 0x%02X req: %d value: %d index: %d length: %d\n",
            setup->bmRequestType, setup->bRequest, setup->wValue, setup->wIndex, setup->wLength);

    return -1;
}

usb_client_callbacks_t client_test_callbacks = {
    .control = client_test_control,
};

static void usb_client_test_unbind(mx_device_t* device) {
    usb_client_test_t* test = get_usb_client_test(device);
    device_remove(&test->device);
}

static mx_status_t usb_client_test_release(mx_device_t* device) {
//    usb_client_test_t* test = get_usb_client_test(device);
    return NO_ERROR;
}

static mx_protocol_device_t usb_client_test_proto = {
    .unbind = usb_client_test_unbind,
    .release = usb_client_test_release,
};

mx_status_t usb_client_bind(mx_driver_t* driver, mx_device_t* parent, void** cookie) {
    printf("usb_client_bind\n");

    usb_client_protocol_t* client_protocol;
    if (device_get_protocol(parent, MX_PROTOCOL_USB_CLIENT, (void**)&client_protocol)) {
        return ERR_NOT_SUPPORTED;
    }

    usb_client_test_t* test = calloc(1, sizeof(usb_client_test_t));
    if (!test) {
        return ERR_NO_MEMORY;
    }
    test->client_device = parent;
    test->client_protocol = client_protocol;

    client_protocol->set_callbacks(parent, &client_test_callbacks, test);

    device_init(&test->device, driver, "usb-client-test", &usb_client_test_proto);
    test->device.protocol_id = MX_PROTOCOL_MISC;
    test->device.protocol_ops = NULL;

    return device_add(&test->device, parent);
}

mx_driver_t _driver_usb_client_test = {
    .name = "msm-fb",
    .ops = {
        .bind = usb_client_bind,
    },
};

// clang-format off

MAGENTA_DRIVER_BEGIN(_driver_usb_client_test, "usb-client-test", "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_USB_CLIENT),
MAGENTA_DRIVER_END(_driver_usb_client_test)
