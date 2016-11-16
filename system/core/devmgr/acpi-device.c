// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/acpi.h>

#include <acpisvc/simple.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>

#include "acpi.h"
#include "devhost.h"

typedef struct acpi_device {
    mx_device_t device;
    char hid[8];
    acpi_handle_t handle;
} acpi_device_t;

#define get_acpi_device(dev) containerof(dev, acpi_device_t, device)

static mx_handle_t acpi_device_clone_handle(mx_device_t* dev) {
    acpi_device_t* device = get_acpi_device(dev);
    return acpi_clone_handle(&device->handle);
}

static mx_acpi_protocol_t acpi_device_acpi_proto = {
    .clone_handle = acpi_device_clone_handle,
};

static mx_protocol_device_t acpi_device_proto = {
};

static mx_status_t acpi_get_child_handle_by_hid(acpi_handle_t* h, const char* hid, acpi_handle_t* child, char* child_name) {
    char name[4] = {0};
    {
        acpi_rsp_list_children_t* rsp;
        size_t len;
        mx_status_t status = acpi_list_children(h, &rsp, &len);
        if (status != NO_ERROR) {
            return status;
        }

        for (uint32_t i = 0; i < rsp->num_children; ++i) {
            if (!memcmp(rsp->children[i].hid, hid, 7)) {
                memcpy(name, rsp->children[i].name, 4);
                break;
            }
        }
        free(rsp);

        if (name[0] == 0) {
            return ERR_NOT_FOUND;
        }
    }
    if (child_name) {
        memcpy(child_name, name, 4);
    }
    return acpi_get_child_handle(h, name, child);
}

static mx_status_t acpi_init_child_device(mx_device_t* parent, mx_driver_t* drv, acpi_handle_t* h, const char* hid) {
    acpi_device_t* dev = calloc(1, sizeof(acpi_device_t));

    char name[4];
    mx_status_t status = acpi_get_child_handle_by_hid(h, hid, &dev->handle, name);
    if (status != NO_ERROR) {
        printf("error getting battery handle %d\n", status);
        free(dev);
        return status;
    }

    memcpy(dev->hid, hid, 7);
    device_init(&dev->device, drv, name, &acpi_device_proto);

    dev->device.protocol_id = MX_PROTOCOL_ACPI;
    dev->device.protocol_ops = &acpi_device_acpi_proto;

    dev->device.props = calloc(2, sizeof(mx_device_prop_t));
    dev->device.props[0].id = BIND_ACPI_HID_0_3;
    dev->device.props[0].value = htobe32(*((uint32_t *)(hid)));
    dev->device.props[1].id = BIND_ACPI_HID_4_7;
    dev->device.props[1].value = htobe32(*((uint32_t *)(hid + 4)));
    dev->device.prop_count = 2;

    if ((status = device_add(&dev->device, parent)) != NO_ERROR) {
        free(dev->device.props);
        free(dev);
    }
    return status;
}

#define ACPI_HID_LID     "PNP0C0D"
#define ACPI_HID_BATTERY "PNP0C0A"

extern mx_handle_t devhost_get_hacpi(void);

static mx_status_t acpi_bind(mx_driver_t* drv, mx_device_t* dev) {
    // TODO(yky,teisenbe) Find the battery device and the lid device. To be replaced by
    // acpi discovery.
    mx_handle_t hacpi = devhost_get_hacpi();
    if (hacpi <= 0) {
        printf("acpi-bus: no acpi root handle\n");
        return ERR_NOT_SUPPORTED;
    }

    acpi_handle_t acpi_root, pcie_handle;
    acpi_handle_init(&acpi_root, hacpi);

    if (acpi_init_child_device(dev, drv, &acpi_root, ACPI_HID_LID) == NO_ERROR) {
        printf("acpi-bus: added lid device\n");
    }

    // TODO(yky,teisenbe) The battery device is in _SB.PCI0 on the acer.
    mx_status_t status = acpi_get_child_handle_by_hid(&acpi_root, "PNP0A08", &pcie_handle, NULL);
    if (status != NO_ERROR) {
        printf("acpi-bus: pcie device not found\n");
        acpi_handle_close(&acpi_root);
        return ERR_NOT_SUPPORTED;
    }
    acpi_handle_close(&acpi_root);

    if (acpi_init_child_device(dev, drv, &pcie_handle, ACPI_HID_BATTERY) == NO_ERROR) {
        printf("acpi-bus: added battery device\n");
    }

    acpi_handle_close(&pcie_handle);
    return NO_ERROR;
}

void devhost_launch_devhost(mx_device_t* parent, const char* name, uint32_t protocol_id, const char* procname, int argc, char** argv);

static mx_status_t acpi_root_init(mx_driver_t* driver) {
    // launch the acpi devhost
    char arg1[20];
    snprintf(arg1, sizeof(arg1), "acpi");
    const char* args[2] = { "/boot/bin/devhost", arg1};
    devhost_launch_devhost(driver_get_root_device(), "acpi", MX_PROTOCOL_ACPI_BUS, "devhost:acpi", 2, (char**)args);
    return NO_ERROR;
}

mx_driver_t _driver_acpi_root = {
    .ops = {
        .init = acpi_root_init,
    },
};

mx_driver_t _driver_acpi = {
    .ops = {
        .bind = acpi_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_acpi, "acpi-bus", "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_ACPI_BUS),
MAGENTA_DRIVER_END(_driver_acpi)
