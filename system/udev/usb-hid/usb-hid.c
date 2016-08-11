// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "device.h"
#include "usb-hid.h"

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/input.h>
#include <ddk/protocol/usb-device.h>

#include <hw/usb.h>

#include <ddk/hexdump.h>
#include <magenta/types.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USB_HID_SUBCLASS_BOOT   0x01
#define USB_HID_PROTOCOL_KBD    0x01
#define USB_HID_DESC_REPORT     0x22
#define USB_HID_OUTPUT_REPORT   0x02
#define USB_HID_FEATURE_REPORT  0x03

#define to_hid_dev(d) containerof(d, usb_hid_dev_t, dev)

static void usb_hid_int_cb(usb_request_t* request) {
    usb_hid_dev_root_t* hid = (usb_hid_dev_root_t*)request->client_data;
#ifdef USB_HID_DEBUG
    printf("usb-hid: callback request status %d\n", request->status);
    hexdump(request->buffer, request->transfer_length);
#endif

    usb_hid_dev_instance_t* instance;
    if (request->status == ERR_CHANNEL_CLOSED) {
        foreach_instance(hid, instance) {
            device_state_set(&instance->dev, DEV_STATE_READABLE);
            instance->flags |= HID_DEAD;
            device_remove(&instance->dev);
        }
        device_remove(&hid->dev);
        return;
    } else if (request->status == NO_ERROR) {
        foreach_instance(hid, instance) {
            mxr_mutex_lock(&instance->fifo.lock);
            bool was_empty = mx_hid_fifo_size(&instance->fifo) == 0;
            ssize_t wrote = 0;
            // Add the report id if it's omitted from the device. This happens if
            // there's only one report and its id is zero.
            if (hid->num_reports == 1 && hid->sizes[0].id == 0) {
                wrote = mx_hid_fifo_write(&instance->fifo, (uint8_t*)&wrote, 1);
                if (wrote <= 0) {
                    printf("could not write report id to usb-hid fifo (ret=%zd)\n", wrote);
                    mxr_mutex_unlock(&instance->fifo.lock);
                    continue;
                }
            }
            wrote = mx_hid_fifo_write(&instance->fifo, request->buffer, request->transfer_length);
            if (wrote <= 0) {
                printf("could not write to usb-hid fifo (ret=%zd)\n", wrote);
            } else {
                if (was_empty) {
                    device_state_set(&instance->dev, DEV_STATE_READABLE);
                }
            }
            mxr_mutex_unlock(&instance->fifo.lock);
        }
    }

    request->transfer_length = request->buffer_length;
    hid->usb->queue_request(hid->usbdev, request);
}

static mx_status_t usb_hid_load_hid_report_desc(usb_interface_t* intf, usb_hid_dev_root_t* hid) {
    for (int i = 0; i < hid->hid_desc->bNumDescriptors; i++) {
        if (hid->hid_desc->descriptors[i].bDescriptorType != USB_HID_DESC_REPORT) continue;
        const size_t len = hid->hid_desc->descriptors[i].wDescriptorLength;
        uint8_t* buf = malloc(len);
        mx_status_t status = hid->usb->control(hid->usbdev, (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE),
                USB_REQ_GET_DESCRIPTOR, USB_HID_DESC_REPORT << 8, hid->interface, buf, len);
        if (status < 0) {
            printf("usb_hid error reading report desc: %d\n", status);
            free(buf);
            return status;
        } else {
            hid_read_report_sizes(hid, buf, len);
            hid->hid_report_desc_len = len;
            hid->hid_report_desc = buf;
#if USB_HID_DEBUG
            printf("usb-hid: dev %p HID descriptor\n", hid);
            hexdump(hid->hid_desc, hid->hid_desc->bLength);
            printf("usb-hid: HID report descriptor\n");
            for (size_t c = 0; c < len; c++) {
                printf("%02x ", buf[c]);
                if (c % 16 == 15) printf("\n");
            }
            printf("\n");
#endif
        }
    }
    return NO_ERROR;
}

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

        usb_hid_dev_root_t* hid = NULL;
        mx_status_t status = usb_hid_create_root(&hid);
        if (hid == NULL) {
            return ERR_NO_MEMORY;
        }

        status = device_init(&hid->dev, drv, "usb-hid", &usb_hid_root_proto);
        if (status != NO_ERROR) {
            usb_hid_cleanup_root(hid);
            return status;
        }

        hid->usbdev = dev;
        hid->drv = drv;
        hid->usb = usb;
        hid->endpt = endpt;
        hid->interface = desc->bInterfaceNumber;

        if (desc->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT) {
            // Use the boot protocol for now
            hid->usb->control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
                    USB_HID_SET_PROTOCOL, 0, i, NULL, 0);
            hid->proto = desc->bInterfaceProtocol;
            if (hid->proto == USB_HID_PROTOCOL_KBD) {
                // Disable numlock on boot
                uint8_t zero = 0;
                hid->usb->control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
                        USB_HID_SET_REPORT, USB_HID_OUTPUT_REPORT << 8, i, &zero, sizeof(zero));
            }
        }

        hid->req = hid->usb->alloc_request(hid->usbdev, hid->endpt, hid->endpt->maxpacketsize);
        if (hid->req == NULL) {
            usb_hid_cleanup_root(hid);
            return ERR_NO_MEMORY;
        }
        hid->req->complete_cb = usb_hid_int_cb;
        hid->req->client_data = hid;

        usb_class_descriptor_t* class_desc = NULL;
        list_for_every_entry(&intf->class_descriptors, class_desc,
                usb_class_descriptor_t, node) {
            if (class_desc->header->bDescriptorType == USB_DT_HID) {
                hid->hid_desc = (usb_hid_descriptor_t*)class_desc->header;
                if (usb_hid_load_hid_report_desc(intf, hid) != NO_ERROR) {
                    hid->hid_desc = NULL;
                    break;
                }
            }
        }
        if (hid->hid_desc == NULL) {
            usb_hid_cleanup_root(hid);
            return ERR_NOT_SUPPORTED;
        }

        hid->dev.protocol_id = MX_PROTOCOL_INPUT;
        status = device_add(&hid->dev, dev);
        if (status != NO_ERROR) {
            usb_hid_cleanup_root(hid);
            return status;
        }

        hid->usb->control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
                USB_HID_SET_IDLE, 0, i, NULL, 0);

        hid->req->transfer_length = hid->req->buffer_length;
        hid->usb->queue_request(hid->usbdev, hid->req);
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
