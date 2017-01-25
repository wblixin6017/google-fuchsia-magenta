// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/compiler.h>
#include <stddef.h>

__BEGIN_CDECLS;

// Placeholder for bootstrap
#define SOC_VID_DEVICETREE 0x00dc
#define SOC_PID_DEVICETREE 0x00dc

typedef struct mx_protocol_devicetree {
    bool (*is_compatible)(mx_device_t* dev, const char* compatible);
    mx_handle_t (*map_mmio)(mx_device_t* dev,
                            const char* name,
                            mx_cache_policy_t cache_policy,
                            void** vaddr,
                            uint64_t* size);
    mx_handle_t (*map_interrupt)(mx_device_t* dev, int which_irq);
    ssize_t (*get_property)(mx_device_t* dev, const char* property, char* buf, size_t count);
} mx_protocol_devicetree_t;

__END_CDECLS;
