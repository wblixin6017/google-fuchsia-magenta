// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/devicetree.h>

#include <libfdt.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct devicetree_device {
    mx_device_t device;
    int offset;
} devicetree_device_t;

#define get_devicetree_device(dev) containerof(dev, devicetree_device_t, device)

static bool devicetree_is_compatible(mx_device_t* dev, const char* compatible) {
    devicetree_device_t* device = get_devicetree_device(dev);
    assert(dev->parent->ctx != NULL);
    return !fdt_node_check_compatible(dev->parent->ctx, device->offset, compatible);
}

static mx_handle_t devicetree_map_mmio(mx_device_t* dev, const char* name, mx_cache_policy_t cache_policy, void** vaddr, uint64_t* size) {
    // return a vmo handle to the memory range specified by the "reg" property
    return ERR_NOT_SUPPORTED;
}

static mx_handle_t devicetree_map_interrupt(mx_device_t* dev, int which_irq) {
    // return an interrupt handle for the irq at which_irq in the "interrupts" property
    return ERR_NOT_SUPPORTED;
}

static ssize_t devicetree_get_property(mx_device_t* dev, const char* property, char* buf, size_t count) {
    // copy the property to buf for a max number of count bytes
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_devicetree_t devicetree_proto = {
    .is_compatible = devicetree_is_compatible,
    .map_mmio = devicetree_map_mmio,
    .map_interrupt = devicetree_map_interrupt,
    .get_property = devicetree_get_property,
};

static mx_protocol_device_t devicetree_device_proto = {
};

// walk the device tree and publish devices under parent
static void devicetree_walk(void* fdt, int* offset, int* depth, int mindepth, int maxdepth, mx_device_t* parent) {
    for (;;) {
        *offset = fdt_next_node(fdt, *offset, depth);
        if (*offset < 0) {
            break;
        }

        if (mindepth >= 0 && *depth <= mindepth) {
            break;
        }

        if (maxdepth >= 0 && *depth > maxdepth) {
            continue;
        }

        const char* name = fdt_get_name(fdt, *offset, NULL);
        if (!name) {
            continue;
        }

        devicetree_device_t* device = calloc(1, sizeof(devicetree_device_t));
        if (!device) {
            printf("devicetree: out of memory\n");
            break;
        }

        device_init(&device->device, parent->driver, name, &devicetree_device_proto);
        device->offset = *offset;
        device->device.protocol_id = MX_PROTOCOL_DEVICETREE;
        device->device.protocol_ops = &devicetree_proto;
        device_add(&device->device, parent);
    }
}

static bool devicetree_find(void* fdt, int* offset, int* depth, const char* name) {
    bool found = false;
    for (;;) {
        *offset = fdt_next_node(fdt, *offset, depth);
        if (*offset < 0) {
            break;
        }

        const char* n = fdt_get_name(fdt, *offset, NULL);
        if (!n) {
            continue;
        }

        if (name && !strcmp(n, name)) {
            found = true;
            break;
        }
    }
    return found;
}

static ssize_t devicetree_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    // FIXME: Temporary to initialize the devicetree with a device tree blob (dtb) on disk.
    // The dtb should be passed as a vmo handle by devmgr.
    if (dev->ctx) {
        return 0;
    }

    int fd = open("/data/devicetree/bb8.dtb", O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    struct stat st;
    int rc = fstat(fd, &st);
    if (rc < 0) {
        close(fd);
        return -1;
    }

    if (st.st_size >= SSIZE_MAX) {
        printf("devicetree: dtb too big\n");
        close(fd);
        return -1;
    }

    void* fdt = malloc(st.st_size);
    if (!fdt) {
        close(fd);
        return -1;
    }

    if (read(fd, fdt, st.st_size) != st.st_size) {
        close(fd);
        return -1;
    }

    rc = fdt_check_header(fdt);
    if (rc < 0) {
        printf("devicetree: fdt bad header\n");
        close(fd);
        return -1;
    }

    dev->ctx = fdt;

    int offset = 0;
    int depth = 0;
    if (devicetree_find(fdt, &offset, &depth, "soc")) {
        devicetree_walk(fdt, &offset, &depth, depth, depth + 1, dev);
    }

    close(fd);
    return count;
}

static mx_protocol_device_t devicetree_root_proto = {
    .read = devicetree_read,
};

static mx_status_t devicetree_bind(mx_driver_t* driver, mx_device_t* device) {
    mx_device_t* dev;
    mx_status_t status = device_create(&dev, driver, "devicetree", &devicetree_root_proto);
    if (status != NO_ERROR) {
        return status;
    }

    status = device_add(dev, device);
    if (status != NO_ERROR) {
        free(dev);
        return status;
    }

    printf("devicetree: driver initialized\n");

    return NO_ERROR;
}

mx_driver_t _driver_devicetree = {
    .name = "devicetree",
    .ops = {
        .bind = devicetree_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_devicetree, "devicetree", "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_SOC),
    BI_MATCH_IF(EQ, BIND_SOC_VID, SOC_VID_DEVICETREE),
    BI_MATCH_IF(EQ, BIND_SOC_PID, SOC_PID_DEVICETREE),
MAGENTA_DRIVER_END(_driver_devicetree)
