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

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/common/hid.h>
#include <ddk/protocol/input.h>
#include <ddk/protocol/usb-device.h>

#include <runtime/mutex.h>
#include <system/listnode.h>

#include <stdint.h>
#include <stddef.h>

typedef struct hid_report_size {
    int16_t id;
    input_report_size_t in_size;
    input_report_size_t out_size;
    input_report_size_t feat_size;
} hid_report_size_t;

typedef struct {
    mx_device_t dev;
    mx_device_t* usbdev;
    mx_driver_t* drv;

    usb_device_protocol_t* usb;
    usb_endpoint_t* endpt;
    usb_request_t* req;

    uint32_t flags;
    uint8_t proto;
    uint8_t interface;

    usb_hid_descriptor_t* hid_desc;
    size_t hid_report_desc_len;
    uint8_t* hid_report_desc;

#define HID_MAX_REPORT_IDS 16
    size_t num_reports;
    hid_report_size_t sizes[HID_MAX_REPORT_IDS];

    // list of opened devices
    struct list_node instance_list;
    mxr_mutex_t instance_lock;
} usb_hid_dev_root_t;

extern mx_protocol_device_t usb_hid_root_proto;

mx_status_t usb_hid_create_root(usb_hid_dev_root_t** dev);
void usb_hid_cleanup_root(usb_hid_dev_root_t* dev);

void hid_read_report_sizes(usb_hid_dev_root_t* hid, const uint8_t* buf, size_t len);

typedef struct {
    mx_device_t dev;
    usb_hid_dev_root_t* root;

    uint32_t flags;

    mx_hid_fifo_t fifo;

    struct list_node node;
} usb_hid_dev_instance_t;

extern mx_protocol_device_t usb_hid_instance_proto;

mx_status_t usb_hid_create_instance(usb_hid_dev_instance_t** dev);
void usb_hid_cleanup_instance(usb_hid_dev_instance_t* dev);

#define foreach_instance(root, instance) \
    list_for_every_entry(&root->instance_list, instance, usb_hid_dev_instance_t, node)
