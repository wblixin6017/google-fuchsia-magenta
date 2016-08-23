// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//#include "device.h"

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/iotxn.h>
#include <ddk/common/usb.h>
#include <ddk/protocol/hid.h>
#include <ddk/protocol/usb-device.h>
#include <hw/usb.h>
#include <hw/usb-hid.h>

#include <ddk/hexdump.h>
#include <magenta/types.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USB_HID_SUBCLASS_BOOT   0x01
#define USB_HID_PROTOCOL_KBD    0x01
#define USB_HID_PROTOCOL_MOUSE  0x02

#define USB_HID_DEBUG 0
#define to_usb_hid(d) containerof(d, usb_hid_device_t, dev)

typedef struct usb_hid_device {
    mx_device_t dev;
    mx_device_t* usbdev;
    mx_driver_t* drv;

    usb_device_protocol_t* usb;
    usb_endpoint_t* endpt;
    usb_request_t* req;

    iotxn_t* txn;

    uint32_t flags;
    uint8_t proto;
    uint8_t interface;

    usb_hid_descriptor_t* hid_desc;
} usb_hid_device_t;

static void usb_interrupt_callback(iotxn_t* txn, void* cookie) {
    usb_hid_device_t* hid = (usb_hid_device_t*)cookie;
    void* buffer;
    txn->ops->mmap(txn, &buffer);
#if USB_HID_DEBUG
    printf("usb-hid: callback request status %d\n", txn->status);
    hexdump(buffer, txn->actual);
#endif

    bool requeue = true;
    switch (txn->status) {
    case ERR_CHANNEL_CLOSED:
        requeue = false;
        break;
    case NO_ERROR:
        if (hid->txn) {
            txn->ops->copyto(hid->txn, buffer, txn->actual, 0);
        }
        break;
    default:
        printf("usb-hid: unknown interrupt status %d\n", txn->status);
        break;
    }
    if (hid->txn) {
        iotxn_t* hidtxn = hid->txn;
        hid->txn = NULL;
        hidtxn->ops->complete(hidtxn, txn->status, txn->actual);
    }

    if (requeue) {
        iotxn_queue(hid->usbdev, hid->txn);
    }
}

static mx_status_t usb_hid_get_descriptor(mx_device_t* dev, uint8_t desc_type,
        void** data, size_t* len) {
    usb_hid_device_t* hid = to_usb_hid(dev);
    int desc_idx = -1;
    for (int i = 0; i < hid->hid_desc->bNumDescriptors; i++) {
        if (hid->hid_desc->descriptors[i].bDescriptorType == desc_type) {
            desc_idx = i;
            break;
        }
    }
    if (desc_idx < 0) {
        return ERR_NOT_FOUND;
    }

    size_t desc_len = hid->hid_desc->descriptors[desc_idx].wDescriptorLength;
    uint8_t* desc_buf = malloc(desc_len);
    mx_status_t status = usb_control(hid->usbdev, (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE),
            USB_REQ_GET_DESCRIPTOR, desc_type << 8, hid->interface, desc_buf, desc_len);
    if (status < 0) {
        printf("usb-hid: error reading report descriptor 0x%02x: %d\n", desc_type, status);
        free(desc_buf);
        return status;
    } else {
        *data = desc_buf;
        *len = desc_len;
    }
    return NO_ERROR;
}

static mx_status_t usb_hid_get_report(mx_device_t* dev, uint8_t rpt_type, uint8_t rpt_id,
        void* data, size_t len) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t usb_hid_set_report(mx_device_t* dev, uint8_t rpt_type, uint8_t rpt_id,
        void* data, size_t len) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t usb_hid_get_idle(mx_device_t* dev, uint8_t rpt_id, uint8_t* duration) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t usb_hid_set_idle(mx_device_t* dev, uint8_t rpt_id, uint8_t duration) {
    usb_hid_device_t* hid = to_usb_hid(dev);
    return usb_control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_SET_IDLE, (duration << 8) | rpt_id, hid->interface, NULL, 0);
}

static mx_status_t usb_hid_get_protocol(mx_device_t* dev, uint8_t* protocol) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t usb_hid_set_protocol(mx_device_t* dev, uint8_t protocol) {
    usb_hid_device_t* hid = to_usb_hid(dev);
    return usb_control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_SET_PROTOCOL, protocol, hid->interface, NULL, 0);
}

static mx_status_t usb_hid_set_interrupt_cb(mx_device_t* dev, hid_interrupt_cb cb) {
    return ERR_NOT_SUPPORTED;
}

static mx_hid_protocol_t hid_proto = {
    .get_descriptor = usb_hid_get_descriptor,
    .get_report = usb_hid_get_report,
    .set_report = usb_hid_set_report,
    .get_idle = usb_hid_get_idle,
    .set_idle = usb_hid_set_idle,
    .get_protocol = usb_hid_get_protocol,
    .set_protocol = usb_hid_set_protocol,
};

static void usb_hid_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    usb_hid_device_t* hid = to_usb_hid(dev);
    hid->txn = txn;
}

static mx_protocol_device_t dev_proto = {
    .iotxn_queue = usb_hid_iotxn_queue,
};

static mx_status_t usb_hid_bind(mx_driver_t* drv, mx_device_t* dev) {
    usb_device_protocol_t* usb;
    if (device_get_protocol(dev, MX_PROTOCOL_USB_DEVICE, (void**)&usb) != NO_ERROR) {
        return ERR_NOT_SUPPORTED;
    }

    usb_device_config_t* devcfg;
    if (usb->get_config(dev, &devcfg) != NO_ERROR) {
        return ERR_NOT_SUPPORTED;
    }
    if (devcfg->num_configurations < 1) {
        return ERR_NOT_SUPPORTED;
    }

    if (devcfg->num_configurations > 1) {
        printf("multiple USB configurations not supported; using first config\n");
    }

    usb_configuration_t* cfg = &devcfg->configurations[0];
    if (cfg->num_interfaces < 1) {
        return ERR_NOT_SUPPORTED;
    }

    // One usb-hid device per HID interface
    for (int i = 0; i < cfg->num_interfaces; i++) {
        usb_interface_t* intf = &cfg->interfaces[i];
        usb_interface_descriptor_t* desc = intf->descriptor;
        assert(intf->num_endpoints == desc->bNumEndpoints);

        if (desc->bInterfaceClass != USB_CLASS_HID) continue;
        if (desc->bNumEndpoints < 1) continue;
        if (list_is_empty(&intf->class_descriptors)) continue;

        usb_endpoint_t* endpt = NULL;
        for (int e = 0; e < intf->num_endpoints; e++) {
            if (intf->endpoints[e].direction == USB_ENDPOINT_IN &&
                intf->endpoints[e].type == USB_ENDPOINT_INTERRUPT) {
                endpt = &intf->endpoints[e];
            }
        }
        if (endpt == NULL) {
            continue;
        }

        usb_hid_device_t* usbhid = calloc(1, sizeof(usb_hid_device_t));
        if (usbhid == NULL) {
            return ERR_NO_MEMORY;
        }

        char name[11];
        snprintf(name, sizeof(name), "usb-hid-%02d", i);
        device_init(&usbhid->dev, drv, name, &dev_proto);
        usbhid->dev.protocol_id = MX_PROTOCOL_HID_BUS;
        usbhid->dev.protocol_ops = &hid_proto;
        mx_status_t status = device_add(&usbhid->dev, dev);
        if (status != NO_ERROR) {
            free(usbhid);
            return status;
        }

        usbhid->usbdev = dev;
        usbhid->drv = drv;
        usbhid->usb = usb;
        usbhid->endpt = endpt;
        usbhid->interface = desc->bInterfaceNumber;

        bool boot_dev = desc->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT;
        uint8_t dev_class = HID_DEV_CLASS_OTHER;
        if (desc->bInterfaceProtocol == USB_HID_PROTOCOL_KBD) {
            dev_class = HID_DEV_CLASS_KBD;
        } else if (desc->bInterfaceProtocol == USB_HID_PROTOCOL_MOUSE) {
            dev_class = HID_DEV_CLASS_POINTER;
        }

        usbhid->txn = usb_alloc_iotxn(usbhid->endpt->descriptor, usbhid->endpt->maxpacketsize, 0);
        if (usbhid->txn == NULL) {
            device_remove(&usbhid->dev);
            free(usbhid);
            return ERR_NO_MEMORY;
        }
        usbhid->txn->complete_cb = usb_interrupt_callback;
        usbhid->txn->cookie = usbhid;

        usb_class_descriptor_t* class_desc = NULL;
        list_for_every_entry(&intf->class_descriptors, class_desc,
                usb_class_descriptor_t, node) {
            if (class_desc->header->bDescriptorType == USB_DT_HID) {
                usbhid->hid_desc = (usb_hid_descriptor_t*)class_desc->header;
                break;
            }
        }
        if (usbhid->hid_desc == NULL) {
            device_remove(&usbhid->dev);
            free(usbhid);
            return ERR_NOT_SUPPORTED;
        }

        usbhid->txn->length = usbhid->endpt->maxpacketsize;
        iotxn_queue(usbhid->usbdev, usbhid->txn);

        mx_hid_device_t* hiddev = NULL;
        status = hid_create_device(&hiddev, &usbhid->dev, i, boot_dev, dev_class);
        if (status != NO_ERROR) {
            device_remove(&usbhid->dev);
            free(usbhid);
            return status;
        }
        status = hid_add_device(drv, hiddev);
        if (status != NO_ERROR) {
            device_remove(&usbhid->dev);
            free(usbhid);
            return status;
        }
    }

    return NO_ERROR;
}

static mx_bind_inst_t binding[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB_DEVICE),
    BI_MATCH_IF(EQ, BIND_USB_CLASS, USB_CLASS_HID),
    BI_ABORT_IF(NE, BIND_USB_CLASS, 0),
    BI_MATCH_IF(EQ, BIND_USB_IFC_CLASS, USB_CLASS_HID),
};

mx_driver_t _driver_usb_hid BUILTIN_DRIVER = {
    .name = "usb-hid",
    .ops = {
        .bind = usb_hid_bind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
