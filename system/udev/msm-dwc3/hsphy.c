// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/driver.h>

#include <hw/arch_ops.h>
#include <hw/reg.h>
#include <magenta/syscalls.h>

#include <unistd.h>

#include "hsphy.h"

#define HSPHY_BASE_PHYS  0x0c012000
#define HSPHY_SIZE_PAGE  0x00001000
#define HSPHY_EFUSE_PHYS 0x00784238

#define HSPHY_EFUSE_TUNE_OFFSET 16
#define HSPHY_EFUSE_TUNE_MASK   0xf

#define HSPHY_PLL_COMMON_STATUS_ONE 0x1a0
#define HSPHY_PLL_COMMON_STATUS_CORE_READY (1 << 0)

#define HSPHY_PWR_CTRL1  0x210
#define HSPHY_PWR_CTRL1_POWR_DOWN (1 << 0)

#define HSPHY_PORT_TUNE1 0x23c
#define HSPHY_PORT_TUNE_OFFSET 4

#define HSPHY_REG(phy, offset) *REG32(phy->regs + (offset))

// for msm8998 v2
// <value> <reg_offset>
static int phy_init_seq[] = {
    0x13, 0x004,
    0x7c, 0x18c,
    0x80, 0x02c,
    0x0a, 0x184,
    0xa5, 0x23c,
    0x09, 0x240,
    0x19, 0x0b4,
};

mx_status_t hsphy_init(hsphy_t** out) {
    hsphy_t* phy = calloc(1, sizeof(hsphy_t));
    if (!phy) {
        return ERR_NO_MEMORY;
    }

    // map hsphy registers
    mx_status_t status = mx_mmap_device_memory(get_root_resource(),
            HSPHY_BASE_PHYS, HSPHY_SIZE_PAGE,
            MX_CACHE_POLICY_UNCACHED_DEVICE, (uintptr_t*)(&phy->regs));
    if (status != NO_ERROR) {
        printf("dwc3: phy: error %d mapping registers\n", status);
        goto err;
    }

#if 0
    if ((status = mx_mmap_device_memory(get_root_resource(),
            HSPHY_EFUSE_PHYS, 0x1000,
            MX_CACHE_POLICY_UNCACHED_DEVICE, (uintptr_t*)(&phy->efuse_reg))) != NO_ERROR) {
        printf("dwc3: phy: error %d mapping efuse register\n", status);
        goto err;
    }
#endif

    // XXX assume that power and clocks are on

    // TODO
    // phy reset

    // disable PHY
    uint32_t reg = HSPHY_REG(phy, HSPHY_PWR_CTRL1);
    HSPHY_REG(phy, HSPHY_PWR_CTRL1) = reg | HSPHY_PWR_CTRL1_POWR_DOWN;

    for (unsigned i = 0; i < (sizeof(phy_init_seq) / sizeof(int)); i += 2) {
        HSPHY_REG(phy, phy_init_seq[i + 1]) = phy_init_seq[i];
        printf("write 0x%x to 0x%x (val 0x%x)\n", phy_init_seq[i], phy_init_seq[i + 1],
                HSPHY_REG(phy, phy_init_seq[i + 1]));
    }

#if 0
    uint32_t efuse = *REG32(phy->efuse_reg);
    printf("dwc3: hsphy: efuse 0x%x\n", efuse);
    uint32_t tune = (efuse >> HSPHY_EFUSE_TUNE_OFFSET) & HSPHY_EFUSE_TUNE_MASK;
    printf("dwc3: hsphy: tune 0x%x\n", tune);
    if (tune) {
        tune = (HSPHY_REG(phy, HSPHY_PORT_TUNE1) & 0xf) | (tune << HSPHY_PORT_TUNE_OFFSET);
    }
    HSPHY_REG(phy, HSPHY_PORT_TUNE1) = tune;

#endif
    HSPHY_REG(phy, HSPHY_PORT_TUNE1) = 0x55;
    // ensure the above is completed before enabling PHY
    hw_wmb();

    // enable PHY
    reg = HSPHY_REG(phy, HSPHY_PWR_CTRL1);
    HSPHY_REG(phy, HSPHY_PWR_CTRL1) = reg & ~HSPHY_PWR_CTRL1_POWR_DOWN;

    // ensure the above is completed before turning on refclk
    hw_wmb();

    // wait for PLL lock
    usleep(160);

    reg = HSPHY_REG(phy, HSPHY_PLL_COMMON_STATUS_ONE);
    printf("dwc3: phy: pll lock 0x%x\n", reg);
    if (!(reg & HSPHY_PLL_COMMON_STATUS_CORE_READY)) {
        printf("dwc3: phy: pll lock fails to lock 0x%x\n", reg);
        printf("dwc3: phy: pwr_ctrl1 0x%x\n", HSPHY_REG(phy, HSPHY_PWR_CTRL1));
    }

    *out = phy;
    return NO_ERROR;
err:
    if (phy) {
        free(phy);
    }
    return status;
}
