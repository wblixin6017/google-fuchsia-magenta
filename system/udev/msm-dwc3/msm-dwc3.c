// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/qcom.h>
#include <ddk/protocol/usb-bus.h>
#include <ddk/protocol/usb-hci.h>
#include <ddk/protocol/usb.h>

#include <hw/reg.h>
#include <magenta/hw/usb-hub.h>
#include <magenta/hw/usb.h>

#include <sched.h>
#include <sync/completion.h>

#include "hsphy.h"

#define DWC3_BASE_PHYS 0x0a800000 // core and msm
#define DWC3_SIZE_PAGE 0x000f9000

#define DWC3_CORE_IRQ (32 + 0x83)

#define DWC3_NUM_EVENT_BUFS     1
#define DWC3_NUM_GSI_EVENT_BUFS 3

#define DWC3_REG(dev, offset) *REG32((dev)->regs + (offset))

#define DWC3_GCTL                (0xc110)
#define DWC3_GCTL_U2RSTECN         (1 << 16)
#define DWC3_GCTL_PRTCAPDIR_MASK   (3 << 12)
#define DWC3_GCTL_PRTCAPDIR_OTG    (3 << 12)
#define DWC3_GCTL_PRTCAPDIR_DEVICE (2 << 12)
#define DWC3_GCTL_PRTCAPDIR_HOST   (1 << 12)
#define DWC3_GCTL_SCALEDOWN(n)     (3 << 4)
#define DWC3_GCTL_SCALEDOWN_MASK   (3 << 4)
#define DWC3_GCTL_U2EXIT_LFPS      (1 << 2)
#define DWC3_GCTL_DSBLCLKGTNG      (1 << 0)

#define DWC3_GSNPSID             (0xc120)
#define DWC3_GHWPARAMS0          (0xc140)
#define DWC3_GHWPARAMS1          (0xc144)
#define DWC3_GHWPARAMS2          (0xc148)

#define DWC3_GHWPARAMS3          (0xc14c)
#define DWC3_GHWPARAMS3_NUM_IN_EPS_OFFSET (18)
#define DWC3_GHWPARAMS3_NUM_IN_EPS_MASK   (0x1f << DWC3_GHWPARAMS3_NUM_IN_EPS_OFFSET)
#define DWC3_GHWPARAMS3_NUM_EPS_OFFSET    (12)
#define DWC3_GHWPARAMS3_NUM_EPS_MASK      (0x3f << DWC3_GHWPARAMS3_NUM_EPS_OFFSET)

#define DWC3_GHWPARAMS4          (0xc150)
#define DWC3_GHWPARAMS5          (0xc154)
#define DWC3_GHWPARAMS6          (0xc158)
#define DWC3_GHWPARAMS7          (0xc15c)

#define DWC3_GUSB2PHYCFG(n)      (0xc200 + ((n) * 0x4))
#define DWC3_GUSB2PHYCFG_SUSPENDUSB20   (1 << 6)

#define DWC3_GUSB3PIPECTL(n)     (0xc2c0 + ((n) * 0x4))
#define DWC3_GUSB3PIPECTL_DELAYP1TRANS  (1 << 18)
#define DWC3_GUSB3PIPECTL_SUSPENDENABLE (1 << 17)

#define DWC3_GEVNTADRLO(n)       (0xc400 + ((n) * 0x10))
#define DWC3_GEVNTADRHI(n)       (0xc404 + ((n) * 0x10))
#define DWC3_GEVNTSIZ(n)         (0xc408 + ((n) * 0x10))
#define DWC3_GEVNTCOUNT(n)       (0xc40c + ((n) * 0x10))

#define DWC3_GHWPARAMS8          (0xc600)

#define DWC3_GFLADJ              (0xc630)
#define DWC3_GFLADJ_REFCLK_LPM_SEL (1 << 23)

#define DWC3_DCFG                (0xc700)
#define DWC3_DCTL                (0xc704)
#define DWC3_DCTL_RUN_STOP       (1 << 31)
#define DWC3_DCTL_CSFTRST        (1 << 30)

#define DWC3_DEVTEN              (0xc708)
#define DWC3_DSTS                (0xc70c)
#define DWC3_DALEPENA            (0xc720)

#define QSCRATCH_BASE_OFFSET     (0x000f8800)
#define QSCRATCH_CGCTL           (QSCRATCH_BASE_OFFSET + 0x28)

#define HI32(val) (((val) >> 32) & 0xffffffff)
#define LO32(val) ((val) & 0xffffffff)

enum dwc3_usb_mode {
    UNKNOWN,
    DEVICE,
    HOST,
    OTG,
};

typedef struct msm_dwc3_device msm_dwc3_device_t;

struct msm_dwc3_device {
    mx_device_t device;

    hsphy_t* hsphy;

    void* regs;

    mx_handle_t irq_handle;
    thrd_t irq_thread;

    uint32_t revision;
    uint32_t hwparams[9];

    io_buffer_t event_buffer;

    int num_in_eps;
    int num_out_eps;
};

#define get_msm_dwc3_device(dev) containerof(dev, msm_dwc3_device_t, device)

static void dwc3_set_mode(msm_dwc3_device_t* dev, enum dwc3_usb_mode mode) {
    uint32_t reg = DWC3_REG(dev, DWC3_GCTL);
    reg &= ~DWC3_GCTL_PRTCAPDIR_MASK;
    switch (mode) {
        case HOST:
            reg |= DWC3_GCTL_PRTCAPDIR_HOST;
            break;
        case DEVICE:
            reg |= DWC3_GCTL_PRTCAPDIR_DEVICE;
            break;
        case OTG:
            reg |= DWC3_GCTL_PRTCAPDIR_OTG;
            break;
        default:
            printf("dwc3_set_mode: unknown mode %d\n", mode);
            break;
    }
    reg |= DWC3_GCTL_U2RSTECN;
    // the following two settings are specific to SS operation
    reg |= DWC3_GCTL_SCALEDOWN(2);
    reg |= DWC3_GCTL_U2EXIT_LFPS;
    printf("dwc3: setting mode to %d gctl 0x%x\n", mode, reg);
    DWC3_REG(dev, DWC3_GCTL) = reg;

    if (mode == OTG || mode == HOST) {
        reg = DWC3_REG(dev, DWC3_GFLADJ);
        DWC3_REG(dev, DWC3_GFLADJ) = reg | DWC3_GFLADJ_REFCLK_LPM_SEL;
    }
}

static void dwc3_core_init(msm_dwc3_device_t* dev) {
    dev->revision = DWC3_REG(dev, DWC3_GSNPSID);
    printf("dwc3: revision 0x%08x\n", dev->revision);

    dev->hwparams[0] = DWC3_REG(dev, DWC3_GHWPARAMS0);
    dev->hwparams[1] = DWC3_REG(dev, DWC3_GHWPARAMS1);
    dev->hwparams[2] = DWC3_REG(dev, DWC3_GHWPARAMS2);
    dev->hwparams[3] = DWC3_REG(dev, DWC3_GHWPARAMS3);
    dev->hwparams[4] = DWC3_REG(dev, DWC3_GHWPARAMS4);
    dev->hwparams[5] = DWC3_REG(dev, DWC3_GHWPARAMS5);
    dev->hwparams[6] = DWC3_REG(dev, DWC3_GHWPARAMS6);
    dev->hwparams[7] = DWC3_REG(dev, DWC3_GHWPARAMS7);
    dev->hwparams[8] = DWC3_REG(dev, DWC3_GHWPARAMS8);
    for (int i = 0; i < 9; i++) {
        printf("dwc3: hwparams%d 0x%08x\n", i, dev->hwparams[i]);
    }

    // PHY init
    uint32_t reg = DWC3_REG(dev, DWC3_GUSB3PIPECTL(0));
    printf("dwc3: usb3pipectl(0) 0x%08x\n", reg);
    DWC3_REG(dev, DWC3_GUSB3PIPECTL(0)) = reg | DWC3_GUSB3PIPECTL_SUSPENDENABLE;

#if 0
    reg = DWC3_REG(dev, DWC3_GUSB2PHYCFG(0));
    printf("dwc3: usb2phycfg(0) 0x%08x\n", reg);
    DWC3_REG(dev, DWC3_GUSB2PHYCFG(0)) = reg | DWC3_GUSB2PHYCFG_SUSPENDUSB20;
#endif

    // TODO
    // allocate event buffers
    //int nbuf = DWC3_NUM_EVENT_BUFS + DWC3_NUM_GSI_EVENT_BUFS;

    // dwc3/core.c, dwc3_core_reset

    // TODO
    // reset SSPHY

    // PHY init
    if (hsphy_init(&dev->hsphy) != NO_ERROR) {
        printf("dwc3: hsphy init failed\n");
    }

    reg = DWC3_REG(dev, DWC3_GUSB3PIPECTL(0));
    DWC3_REG(dev, DWC3_GUSB3PIPECTL(0)) = reg & ~DWC3_GUSB3PIPECTL_DELAYP1TRANS;

    // dwc3/dwc3-msm.c, reset event
    // enable master clock for rams to allow bam to access ram when ram clock gating
    // is enabled via dwc3's GCTL
    reg = DWC3_REG(dev, QSCRATCH_CGCTL);
    printf("dwc3: cgctl 0x%x\n", reg);
    DWC3_REG(dev, QSCRATCH_CGCTL) = reg | 0x18;

    // dwc3/core.c, dwc3_soft_reset
    reg = DWC3_DCTL_CSFTRST;
    DWC3_REG(dev, DWC3_DCTL) = reg;
    for (;;) {
        reg = DWC3_REG(dev, DWC3_DCTL);
        if (!(reg & DWC3_DCTL_CSFTRST)) {
            break;
        }
        sched_yield();
    }

    reg = DWC3_REG(dev, DWC3_GCTL);
    printf("dwc3: gctl 0x%x\n", reg);
    reg &= ~DWC3_GCTL_SCALEDOWN_MASK;
    reg |= DWC3_GCTL_DSBLCLKGTNG;     // disable clock gating
    DWC3_REG(dev, DWC3_GCTL) = reg;

    dev->num_in_eps = (dev->hwparams[3] & DWC3_GHWPARAMS3_NUM_IN_EPS_MASK) >> DWC3_GHWPARAMS3_NUM_IN_EPS_OFFSET;
    dev->num_out_eps = ((dev->hwparams[3] & DWC3_GHWPARAMS3_NUM_EPS_MASK) >> DWC3_GHWPARAMS3_NUM_EPS_OFFSET) - dev->num_in_eps;
    printf("dwc3: %d in eps %d out eps\n", dev->num_in_eps, dev->num_out_eps);

    // TODO
    // allocate scratch buffers
    // setup scratch buffers

    // TODO
    // setup event buffers
    printf("dwc3: %d device interrupts\n", (dev->hwparams[1] >> 17) & 0x3f);

    mx_status_t status = io_buffer_init(&dev->event_buffer, PAGE_SIZE, IO_BUFFER_RW);
    if (status < 0) {
        printf("dwc3: error %d allocating event buffer\n", status);
    }

    void* ptr = io_buffer_virt(&dev->event_buffer);
    memset(ptr, 0, PAGE_SIZE);

    mx_paddr_t phys = io_buffer_phys(&dev->event_buffer);
    DWC3_REG(dev, DWC3_GEVNTADRLO(0)) = LO32(phys);
    DWC3_REG(dev, DWC3_GEVNTADRHI(0)) = HI32(phys);
    DWC3_REG(dev, DWC3_GEVNTSIZ(0)) = PAGE_SIZE & 0xffff;
    DWC3_REG(dev, DWC3_GEVNTCOUNT(0)) = 0;
    printf("dwc3: event buffer at phys 0x%" PRIx64 " virt %p\n", phys, ptr);

    dwc3_set_mode(dev, DEVICE);

    reg = DWC3_REG(dev, DWC3_DCFG);
    DWC3_REG(dev, DWC3_DCFG) = reg & ~0x7; // highspeed
    printf("dwc3: dcfg 0x%08x\n", reg);

    DWC3_REG(dev, DWC3_DEVTEN) = 0xffffffff;

    DWC3_REG(dev, DWC3_DALEPENA) = 0x3;

    reg = DWC3_REG(dev, DWC3_DCTL);
    DWC3_REG(dev, DWC3_DCTL) = reg | DWC3_DCTL_RUN_STOP;
    printf("dwc3: dctl 0x%x\n", DWC3_REG(dev, DWC3_DCTL));

    mx_nanosleep(MX_MSEC(1000));
    printf("dwc3: dsts 0x%08x\n", DWC3_REG(dev, DWC3_DSTS));
}


static int dwc3_irq_thread(void* arg) {
    msm_dwc3_device_t* dwc = (msm_dwc3_device_t*)arg;
    assert(dwc->irq_handle != MX_HANDLE_INVALID);

    printf("dwc3: irq thread start\n");

    for (;;) {
        mx_status_t status = mx_interrupt_wait(dwc->irq_handle);
        if (status != NO_ERROR) {
            printf("dwc3: error %d waiting for core interrupt\n", status);
            continue;
        }
        printf("dwc3: got core irq\n");
        mx_interrupt_complete(dwc->irq_handle);
    }
    return 0;
}

static mx_status_t msm_dwc3_bind(mx_driver_t* drv, mx_device_t* dev, void** cookie) {
    msm_dwc3_device_t* dwc = calloc(1, sizeof(msm_dwc3_device_t));
    if (!dwc) {
        return ERR_NO_MEMORY;
    }

    // map dwc3 core registers
    mx_status_t status = mx_mmap_device_memory(get_root_resource(),
            DWC3_BASE_PHYS, DWC3_SIZE_PAGE,
            MX_CACHE_POLICY_UNCACHED_DEVICE, (uintptr_t*)(&dwc->regs));
    if (status != NO_ERROR) {
        printf("dwc3: error %d mapping registers\n", status);
        goto err;
    }

    // get dwc3 core irq handle
    dwc->irq_handle = mx_interrupt_create(get_root_resource(), DWC3_CORE_IRQ, MX_FLAG_REMAP_IRQ);
    if (dwc->irq_handle < 0) {
        dwc->irq_handle = MX_HANDLE_INVALID;
        status = ERR_NO_RESOURCES;
        printf("dwc3: error %d requesting irq\n", status);
        goto err;
    }

    // create irq thread
    if (thrd_create_with_name(&dwc->irq_thread, dwc3_irq_thread, dwc, "msm_dwc3_irq_thread") != thrd_success) {
        status = ERR_NO_RESOURCES;
        printf("dwc3: error creating irq thread\n");
        goto err;
    }

    // initialize dwc3 core
    // TODO otg
    dwc3_core_init(dwc);

    return NO_ERROR;
err:
    if (dwc) {
        if (dwc->irq_handle != MX_HANDLE_INVALID) {
            mx_handle_close(dwc->irq_handle);
        }
        free(dwc);
    }
    printf("dwc3: bind error %d\n", status);
    return status;
}

mx_driver_t _driver_msm_dwc3 = {
    .ops = {
        .bind = msm_dwc3_bind,
    },
};

// The formatter does not play nice with these macros.
// clang-format off
MAGENTA_DRIVER_BEGIN(_driver_msm_dwc3, "msm-dwc3", "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_SOC),
    BI_ABORT_IF(NE, BIND_SOC_VID, SOC_VID_QCOM),
    BI_MATCH_IF(EQ, BIND_SOC_DID, SOC_DID_QCOM_DWC3),
MAGENTA_DRIVER_END(_driver_msm_dwc3)
// clang-format on
