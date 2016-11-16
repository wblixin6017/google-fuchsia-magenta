// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/acpi.h>
#include <ddk/common/hid.h>

#include <acpisvc/simple.h>
#include <magenta/types.h>
#include <magenta/syscalls.h>
#include <stdlib.h>
#include <stdio.h>
#include <threads.h>

typedef struct acpi_lid_device {
    mx_device_t device;
    acpi_handle_t acpi_handle;
    thrd_t event_thread;
} acpi_lid_device_t;

static mx_protocol_device_t acpi_lid_device_proto = {
};

static int acpi_lid_event_thread(void* arg) {
    acpi_lid_device_t* dev = arg;
    if (dev->acpi_handle.notify == MX_HANDLE_INVALID) {
        printf("acpi-lid: notify handle is uninitialized!\n");
        return 1;
    }
    for (;;) {
        mx_signals_t pending;
        mx_status_t status = mx_handle_wait_one(dev->acpi_handle.notify, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED, MX_TIME_INFINITE, &pending);
        if (status != NO_ERROR) {
            continue;
        }
        if (pending & MX_SIGNAL_READABLE) {
            acpi_event_t evt;
            uint32_t size = sizeof(evt);
            status = mx_channel_read(dev->acpi_handle.notify, 0, &evt, size, &size, NULL, 0, NULL);
            if (status == ERR_BAD_STATE) {
                continue;
            }
            if (size != sizeof(evt)) {
                printf("acpi-lid: unexpected acpi event size %d (expected %zd)\n", size, sizeof(evt));
                continue;
            }
            if (evt.type == ACPI_EVENT_DEVICE_NOTIFY && evt.arg == 0x80) {
                // read the lid status
                acpi_rsp_lid_t* rsp;
                status = acpi_lid(&dev->acpi_handle, &rsp);
                if (status == NO_ERROR) {
                }
                free(rsp);
            }
        }
        if (pending & MX_SIGNAL_PEER_CLOSED) {
            break;
        }
    }
    printf("acpi-lid: event thread exiting\n");
    return 0;
}

static mx_status_t acpi_lid_bind(mx_driver_t* drv, mx_device_t* dev) {

    mx_acpi_protocol_t* acpi;
    if (device_get_protocol(dev, MX_PROTOCOL_ACPI, (void**)&acpi)) {
        return ERR_NOT_SUPPORTED;
    }

    mx_handle_t handle = acpi->clone_handle(dev);
    if (handle <= 0) {
        printf("acpi-lid: error cloning handle (%d)\n", handle);
        return handle;
    }

    acpi_lid_device_t* device = calloc(1, sizeof(acpi_lid_device_t));
    if (!device) {
        mx_handle_close(handle);
        return ERR_NO_MEMORY;
    }
    acpi_handle_init(&device->acpi_handle, handle);

    mx_status_t status = acpi_enable_event(&device->acpi_handle, ACPI_EVENT_DEVICE_NOTIFY);
    if (status != NO_ERROR) {
        printf("acpi-lid: error %d enabling device event\n", status);
    }

    int rc = thrd_create_with_name(&device->event_thread, acpi_lid_event_thread, device, "acpi-lid-event");
    if (rc != thrd_success) {
        printf("acpi-lid: event thread did not start (%d)\n", rc);
    }

    device_init(&device->device, drv, "acpi-lid", &acpi_lid_device_proto);
    device_add(&device->device, dev);

    printf("acpi-lid: lid device initialized\n");

    return NO_ERROR;
}

mx_driver_t _driver_acpi_lid = {
    .ops = {
        .bind = acpi_lid_bind,
    },
};

#define ACPI_LID_HID_0_3 0x504e5030 // "PNP0"
#define ACPI_LID_HID_4_7 0x43304400 // "C0D"

MAGENTA_DRIVER_BEGIN(_driver_acpi_lid, "acpi-lid", "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_ACPI),
    BI_ABORT_IF(NE, BIND_ACPI_HID_0_3, ACPI_LID_HID_0_3),
    BI_MATCH_IF(EQ, BIND_ACPI_HID_4_7, ACPI_LID_HID_4_7),
MAGENTA_DRIVER_END(_driver_acpi_lid)
