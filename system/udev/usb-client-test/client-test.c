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

static const usb_device_descriptor_t device_desc = {
    .bLength = sizeof(usb_device_descriptor_t),
    .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = htole16(0x0200),
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 64,
    .idVendor = htole16(0x18D1),
    .idProduct = htole16(0x1234),
    .bcdDevice = htole16(0x0100),
    .iManufacturer = 0,
    .iProduct = 0,
    .iSerialNumber = 0,
    .bNumConfigurations = 1,
};

static const struct {
    usb_configuration_descriptor_t config;
    usb_interface_descriptor_t intf;
    usb_endpoint_descriptor_t endp1;
    usb_endpoint_descriptor_t endp2;
} config_desc = {
     .config = {
        .bLength = sizeof(usb_configuration_descriptor_t),
        .bDescriptorType = USB_DT_CONFIG,
        .wTotalLength = htole16(sizeof(config_desc)),
        .bNumInterfaces = 1,
        .bConfigurationValue = 1,
        .iConfiguration = 0,
        .bmAttributes = 0xE0,   // self powered
        .bMaxPower = 0,
    },
    .intf = {
        .bLength = sizeof(usb_interface_descriptor_t),
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bAlternateSetting = 0,
        .bNumEndpoints = 1,
        .bInterfaceClass = 255,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface = 0,
    },
    .endp1 = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_IN | 1,
        .bmAttributes = USB_ENDPOINT_BULK,
        .wMaxPacketSize = htole16(512),
        .bInterval = 0,
    },
    .endp2 = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_OUT | 1,
        .bmAttributes = USB_ENDPOINT_BULK,
        .wMaxPacketSize = htole16(512),
        .bInterval = 0,
    },
};

typedef struct {
    mx_device_t device;
    mx_device_t* client_device;
    usb_client_protocol_t* client_protocol;

} usb_client_test_t;
#define get_usb_client_test(dev) containerof(dev, usb_client_test_t, device)

static mx_status_t usb_client_get_descriptor(uint8_t request_type, uint16_t value, uint16_t index,
                                             void* buffer, uint length) {
    uint8_t type = request_type & USB_TYPE_MASK;
    uint8_t recipient = request_type & USB_RECIP_MASK;

    if (type == USB_TYPE_STANDARD && recipient == USB_RECIP_DEVICE) {
        uint8_t desc_type = value >> 8;
        if (desc_type == USB_DT_DEVICE && index == 0) {
            if (length > sizeof(device_desc)) length = sizeof(device_desc);
            memcpy(buffer, &device_desc, length);
            return length;
        } else if (desc_type == USB_DT_CONFIG && index == 0) {
            if (length > sizeof(config_desc)) length = sizeof(config_desc);
            memcpy(buffer, &config_desc, length);
            return length;
        }
 /*        else if (value >> 8 == USB_DT_STRING) {
            uint8_t string_index = value & 0xFF;
            if (string_index < countof(xhci_rh_string_table)) {
                const uint8_t* string = xhci_rh_string_table[string_index];
                if (length > string[0]) length = string[0];

                txn->ops->copyto(txn, string, length, 0);
                txn->ops->complete(txn, NO_ERROR, length);
                return NO_ERROR;
            }
        }*/
    }

    printf("usb_client_get_descriptor unsupported value: %d index: %d\n", value, index);
    return ERR_NOT_SUPPORTED;
}


static mx_status_t client_test_control(const usb_setup_t* setup, void* buffer, int buffer_length, void* cookie) {
    uint8_t request_type = setup->bmRequestType;
    uint8_t request = setup->bRequest;
    uint16_t value = le16toh(setup->wValue);
    uint16_t index = le16toh(setup->wIndex);
    uint16_t length = le16toh(setup->wLength);
    if (length > buffer_length) length = buffer_length;

    printf("client_test_control type: 0x%02X req: %d value: %d index: %d length: %d\n",
            request_type, request, value, index, length);

    if ((request_type & USB_DIR_MASK) == USB_DIR_IN && request == USB_REQ_GET_DESCRIPTOR) {
        return usb_client_get_descriptor(request_type, value, index, buffer, length);
    } else if (request_type == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE) &&
               request == USB_REQ_SET_CONFIGURATION && length == 0) {
        return NO_ERROR;
    }

    return ERR_NOT_SUPPORTED;
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
