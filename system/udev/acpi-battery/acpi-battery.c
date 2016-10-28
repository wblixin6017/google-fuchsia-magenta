// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/acpi.h>

#include <acpisvc/simple.h>
#include <magenta/types.h>
#include <magenta/syscalls.h>

#define TRACE 1

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

typedef struct acpi_battery_device {
    mx_device_t device;
    acpi_handle_t acpi_handle;
} acpi_battery_device_t;

#define get_acpi_battery_device(dev) containerof(dev, acpi_battery_device_t, device)

static ssize_t acpi_battery_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
}

static mx_status_t acpi_battery_release(mx_device_t* dev) {
    acpi_battery_device_t* device = get_acpi_battery_device(dev);
    acpi_handle_close(&device->acpi_handle);
    return NO_ERROR;
}

static mx_protocol_device_t acpi_battery_device_proto = {
    .read = acpi_battery_read,
    .release = acpi_battery_release,
};

static mx_status_t acpi_battery_bind(mx_driver_t* drv, mx_device_t* dev) {
    acpi_protocol_t* acpi;
    if (device_get_protocol(dev, MX_PROTOCOL_ACPI, (void**)&acpi)) {
        return ERR_NOT_SUPPORTED;
    }

    mx_handle_t handle = acpi->clone_handle(dev);
    if (handle <= 0) {
        printf("acpi-battery: error cloning handle\n");
        return handle;
    }

    acpi_battery_device_t* device = calloc(1, sizeof(acpi_battery_device_t));
    if (!device) {
        mx_handle_close(handle);
        return ERR_NO_MEMORY;
    }
    acpi_handle_init(&device->acpi_handle, handle);

    device_init(&device->device, drv, "acpi-battery", &acpi_battery_device_proto);
    device_add(&device->device, dev);
    return NO_ERROR;
}

mx_driver_t _driver_acpi_battery = {
    .ops = {
        .bind = acpi_battery_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_acpi_battery, "acpi-battery", "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_ACPI),
MAGENTA_DRIVER_END(_driver_acpi_battery)
