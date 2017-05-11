// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>

// private API in ulib/driver
extern mx_handle_t driver_get_mdi_handle(void);

typedef struct {
    mx_device_t* mxdev;
} platform_bus_t;

typedef struct {
    mx_device_t* mxdev;
    mx_device_prop_t props[3];
} platform_dev_t;

static void platform_dev_release(void* ctx) {
    platform_dev_t* dev = ctx;
    free(dev);
}

static mx_protocol_device_t platform_dev_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = platform_dev_release,
};

static void platform_bus_release(void* ctx) {
    platform_bus_t* bus = ctx;
    free(bus);
}

static mx_protocol_device_t platform_bus_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = platform_bus_release,
};

static void platform_bus_publish_devices(mdi_node_ref_t* bus_node, mx_device_t* parent,
                                         mx_driver_t* driver) {
    mdi_node_ref_t  driver_node;
    mdi_each_child(bus_node, &driver_node) {
        if (mdi_id(&driver_node) != MDI_PLATFORM_BUS_DRIVER) {
            printf("unexpected node %d in platform_bus_publish_devices\n", mdi_id(&driver_node));
            continue;
        }
        uint32_t vid = 0;
        uint32_t pid = 0;
        uint32_t did = 0;
        mdi_node_ref_t  node;
        mdi_each_child(&driver_node, &node) {
            switch (mdi_id(&node)) {
                case MDI_PLATFORM_BUS_DRIVER_VID:
                mdi_node_uint32(&node, &vid);
                break;
            case MDI_PLATFORM_BUS_DRIVER_PID:
                mdi_node_uint32(&node, &pid);
                break;
            case MDI_PLATFORM_BUS_DRIVER_DID:
                mdi_node_uint32(&node, &did);
                break;
            default:
                break;
            }

            if (!vid || !pid || !did) {
                printf("missing vid pid or did\n");
                continue;
            }

            platform_dev_t* dev = calloc(1, sizeof(platform_dev_t));
            if (!dev) return;

            mx_device_prop_t props[] = {
                {BIND_SOC_VID, 0, vid},
                {BIND_SOC_PID, 0, pid},
                {BIND_SOC_DID, 0, did},
            };
            static_assert(countof(props) == countof(dev->props), "");
            memcpy(dev->props, props, sizeof(dev->props));

            char name[50];
            snprintf(name, sizeof(name), "pdev-%u:%u:%u\n", vid, pid, did);

            device_add_args_t args = {
                .version = DEVICE_ADD_ARGS_VERSION,
                .name = name,
                .ctx = dev,
                .driver = driver,
                .ops = &platform_dev_proto,
                .proto_id = MX_PROTOCOL_SOC,
                .props = dev->props,
                .prop_count = countof(dev->props),
            };

            mx_status_t status = device_add(parent, &args, &dev->mxdev);
            if (status != NO_ERROR) {
                printf("platform-bus failed to create device for %u:%u:%u\n", vid, pid, did);
            } else {
                printf("platform-bus added device %s\n", name);
            }
        }
    }
}

static mx_status_t platform_bus_add_root_device(mx_driver_t* driver, mx_device_t* parent,
                                                mx_device_t** out_device) {
    platform_bus_t* bus = calloc(1, sizeof(platform_dev_t));
    if (!bus) {
        return ERR_NO_MEMORY;
    }

    device_add_args_t add_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "platform-bus",
        .ctx = bus,
        .driver = driver,
        .ops = &platform_bus_proto,
    };
    mx_status_t status = device_add(parent, &add_args, &bus->mxdev);
    if (status != NO_ERROR) {
        free(bus);
        return status;
    }

    if (out_device) {
        *out_device = bus->mxdev;
    }

    return NO_ERROR;
}

static mx_status_t platform_bus_bind(mx_driver_t* driver, mx_device_t* parent, void** cookie) {
    printf("platform_bus_bind\n");

    mx_handle_t mdi_handle = driver_get_mdi_handle();
    if (mdi_handle == MX_HANDLE_INVALID) {
        printf("platform_bus_bind mdi_handle invalid\n");
        return ERR_NOT_SUPPORTED;
    }

    void* addr = NULL;
    size_t size;
    mx_status_t status = mx_vmo_get_size(mdi_handle, &size);
    if (status != NO_ERROR) {
        printf("platform_bus_bind mx_vmo_get_size failed %d\n", status);
        goto fail;
    }
    status = mx_vmar_map(mx_vmar_root_self(), 0, mdi_handle, 0, size, MX_VM_FLAG_PERM_READ,
                         (uintptr_t *)&addr);
    if (status != NO_ERROR) {
        printf("platform_bus_bind mx_vmar_map failed %d\n", status);
        goto fail;
    }

    mdi_node_ref_t root_node;
    status = mdi_init(addr, size, &root_node);
    if (status != NO_ERROR) {
        printf("platform_bus_bind mdi_init failed %d\n", status);
        goto fail;
    }

    mdi_node_ref_t  bus_node;
    if (mdi_find_node(&root_node, MDI_PLATFORM_BUS, &bus_node) != NO_ERROR) {
        printf("platform_bus_bind couldn't find MDI_PLATFORM_BUS\n");
        goto fail;
    }

    mx_device_t* root_device;
    status = platform_bus_add_root_device(driver, parent, &root_device);
    if (status != NO_ERROR) {
        goto fail;
    }

    platform_bus_publish_devices(&bus_node, root_device, driver);
    printf("platform_bus_bind SUCCESS\n");

    return NO_ERROR;

fail:
    if (addr) {
        mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)addr, size);
    }
    mx_handle_close(mdi_handle);
    return status;
}

static mx_status_t platform_bus_create(mx_driver_t* driver, mx_device_t* parent,
                                   const char* name, const char* args, mx_handle_t resource) {
    printf("platform_bus_create\n");

    if (resource != MX_HANDLE_INVALID) {
        mx_handle_close(resource);
    }

    return platform_bus_add_root_device(driver, parent, NULL);
}

static mx_driver_ops_t platform_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = platform_bus_bind,
    .create = platform_bus_create,
};

MAGENTA_DRIVER_BEGIN(platform_bus, platform_bus_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_ROOT),
MAGENTA_DRIVER_END(platform_bus)
