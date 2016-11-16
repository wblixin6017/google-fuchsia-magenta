// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/acpi.h>
#include <ddk/protocol/input.h>

#include <acpisvc/simple.h>
#include <magenta/types.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>
#include <stdlib.h>
#include <stdio.h>
#include <threads.h>

typedef struct acpi_lid_device {
    mx_device_t device;
    acpi_handle_t acpi_handle;

    thrd_t event_thread;

    boot_kbd_report_t report;
} acpi_lid_device_t;

static mx_protocol_device_t acpi_lid_device_proto = {
};

static int acpi_lid_event_thread(void* arg) {
    acpi_lid_device_t* dev = arg;
    acpi_event_packet_t pkt;
    printf("acpi_lid: event thread start\n");
    for (;;) {
        mx_status_t status = mx_port_wait(dev->acpi_handle.notify, MX_TIME_INFINITE, &pkt, sizeof(pkt));
        if (status != NO_ERROR) {
            continue;
        }
        printf("acpi-lid: got event type=0x%x arg=0x%x\n", pkt.type, pkt.arg);

        acpi_rsp_lid_t* rsp;
        if ((status = acpi_lid(&dev->acpi_handle, &rsp)) != NO_ERROR) {
            continue;
        }
        printf("acpi-lid: open=%d\n", rsp.open);
    }
    return 0;
}

static mx_status_t acpi_lid_bind(mx_driver_t* drv, mx_device_t* dev) {
    printf("acpi-lid: bind\n");

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

    printf("acpi-lid: lid device found\n");

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
