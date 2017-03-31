// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <hw/pci.h>
#include <magenta/compiler.h>
#include <magenta/syscalls/pci.h>
#include <magenta/types.h>

__BEGIN_CDECLS;

/**
 * protocols/pci.h - PCI protocol definitions
 *
 * The PCI host driver publishes mx_device_t's with its config set to a pci_device_config_t.
 */

// This is used to handle calling map_resource without attempting to change
// the cache policy when we know the bus driver has configured it already.
#define PCI_CACHE_POLICY_BUS_DRIVER     (MX_CACHE_POLICY_MASK + 1u)
// TODO: someday maybe we can centralize this
#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))

typedef struct pci_protocol {
    mx_status_t (*claim_device)(mx_device_t* dev);
    mx_status_t (*map_resource)(mx_device_t* dev,
                                mx_pci_resource_t* resource,
                                uint32_t cache_policy,
                                void** vaddr);
    mx_status_t (*enable_bus_master)(mx_device_t* dev, bool enable);
    mx_status_t (*enable_pio)(mx_device_t* dev, bool enable);
    mx_status_t (*reset_device)(mx_device_t* dev);
    mx_status_t (*map_interrupt)(mx_device_t* dev, int which_irq, mx_handle_t* out_handle);
    mx_status_t (*get_config)(mx_device_t* dev,
                              mx_pci_resource_t* out_handle);
    mx_status_t (*get_bar)(mx_device_t* dev,
                              uint32_t bar_num,
                              mx_pci_resource_t* out_bar);
    mx_status_t (*query_irq_mode_caps)(mx_device_t* dev,
                                       mx_pci_irq_mode_t mode,
                                       uint32_t* out_max_irqs);
    mx_status_t (*set_irq_mode)(mx_device_t* dev,
                                mx_pci_irq_mode_t mode,
                                uint32_t requested_irq_count);
} pci_protocol_t;

__END_CDECLS;
