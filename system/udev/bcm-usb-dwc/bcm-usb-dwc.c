// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// Standard Includes
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

// DDK includes
#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/protocol/bcm.h>
#include <ddk/protocol/usb-bus.h>
#include <ddk/protocol/usb-hci.h>
#include <ddk/protocol/usb.h>

#include <magenta/hw/usb-hub.h>
#include <magenta/hw/usb.h>

#include "../bcm-common/bcm28xx.h"
#include "usb_dwc_regs.h"

#define dev_to_usb_dwc(dev) containerof(dev, usb_dwc_t, device)

#define NUM_HOST_CHANNELS  8
#define PAGE_MASK_4K       (0xFFF)
#define USB_PAGE_START     (USB_BASE & (~PAGE_MASK_4K))
#define USB_PAGE_SIZE      (0x4000)
#define PAGE_REG_DELTA     (USB_BASE - USB_PAGE_START)

#define MAX_DEVICE_COUNT   65
#define ROOT_HUB_DEVICE_ID (MAX_DEVICE_COUNT - 1)

static volatile struct dwc_regs* regs;

#define TRACE 1
#include "bcm-usb-dwc-debug.h"

typedef struct usb_dwc_transfer_request {
    list_node_t node;

    uint8_t bMaxPacketSize0;

    iotxn_t *txn;
} usb_dwc_transfer_request_t;

typedef struct usb_dwc {
    mx_device_t device;
    mx_device_t* bus_device;
    usb_bus_protocol_t* bus_protocol;
    mx_handle_t irq_handle;
    thrd_t irq_thread;
    mx_device_t* parent;

    // Pertaining to root hub transactions.
    mtx_t rh_txn_mtx;
    completion_t rh_txn_completion;
    list_node_t rh_txn_head;
} usb_dwc_t;

typedef struct dwc_channel_context {
    uint8_t channel_id;
    usb_dwc_transfer_request_t* active_request;

    completion_t request_ready_completion;
    completion_t transaction_finished_completion;

    thrd_t channel_thread;
} dwc_channel_context_t;

static mtx_t rh_status_mtx;
static usb_dwc_transfer_request_t* rh_intr_req;
static usb_port_status_t root_port_status;

static mtx_t pending_transfer_mtx;
static completion_t pending_transfer_completion = COMPLETION_INIT;
static list_node_t pending_transfer_list;

static dwc_channel_context_t channel_context[NUM_HOST_CHANNELS];

#define ALL_CHANNELS_FREE 0xff
static mtx_t free_channel_mtx;
static completion_t free_channel_completion = COMPLETION_INIT;
static uint8_t free_channels = ALL_CHANNELS_FREE;
static uint acquire_channel_blocking(void);
static void release_channel(uint ch);

#define MANUFACTURER_STRING 1
#define PRODUCT_STRING_2    2

static const uint8_t dwc_language_list[] =
    { 4, /* bLength */ USB_DT_STRING, 0x09, 0x04, /* language ID */ };
static const uint8_t dwc_manufacturer_string [] = // "Magenta"
    { 18, /* bLength */ USB_DT_STRING, 'M', 0, 'a', 0, 'g', 0, 'e', 0, 'n', 0, 't', 0, 'a', 0, 0, 0 };
static const uint8_t dwc_product_string_2 [] = // "USB 2.0 Root Hub"
    { 36, /* bLength */ USB_DT_STRING, 'U', 0, 'S', 0, 'B', 0, ' ', 0, '2', 0, '.', 0, '0', 0,' ', 0,
                        'R', 0, 'o', 0, 'o', 0, 't', 0, ' ', 0, 'H', 0, 'u', 0, 'b', 0, 0, 0, };

static const uint8_t* dwc_rh_string_table[] = {
    dwc_language_list,
    dwc_manufacturer_string,
    dwc_product_string_2,
};

// device descriptor for USB 2.0 root hub
// represented as a byte array to avoid endianness issues
static const uint8_t dwc_rh_descriptor[sizeof(usb_device_descriptor_t)] = {
    sizeof(usb_device_descriptor_t),    // bLength
    USB_DT_DEVICE,                      // bDescriptorType
    0x00, 0x02,                         // bcdUSB = 2.0
    USB_CLASS_HUB,                      // bDeviceClass
    0,                                  // bDeviceSubClass
    1,                                  // bDeviceProtocol = Single TT
    64,                                 // bMaxPacketSize0
    0xD1, 0x18,                         // idVendor = 0x18D1 (Google)
    0x02, 0xA0,                         // idProduct = 0xA002
    0x00, 0x01,                         // bcdDevice = 1.0
    MANUFACTURER_STRING,                // iManufacturer
    PRODUCT_STRING_2,                   // iProduct
    0,                                  // iSerialNumber
    1,                                  // bNumConfigurations
};

#define CONFIG_DESC_SIZE sizeof(usb_configuration_descriptor_t) + \
                         sizeof(usb_interface_descriptor_t) + \
                         sizeof(usb_endpoint_descriptor_t)

// we are currently using the same configuration descriptors for both USB 2.0 and 3.0 root hubs
// this is not actually correct, but our usb-hub driver isn't sophisticated enough to notice
static const uint8_t dwc_rh_config_descriptor[CONFIG_DESC_SIZE] = {
    // config descriptor
    sizeof(usb_configuration_descriptor_t),    // bLength
    USB_DT_CONFIG,                             // bDescriptorType
    CONFIG_DESC_SIZE, 0,                       // wTotalLength
    1,                                         // bNumInterfaces
    1,                                         // bConfigurationValue
    0,                                         // iConfiguration
    0xE0,                                      // bmAttributes = self powered
    0,                                         // bMaxPower
    // interface descriptor
    sizeof(usb_interface_descriptor_t),         // bLength
    USB_DT_INTERFACE,                           // bDescriptorType
    0,                                          // bInterfaceNumber
    0,                                          // bAlternateSetting
    1,                                          // bNumEndpoints
    USB_CLASS_HUB,                              // bInterfaceClass
    0,                                          // bInterfaceSubClass
    0,                                          // bInterfaceProtocol
    0,                                          // iInterface
    // endpoint descriptor
    sizeof(usb_endpoint_descriptor_t),          // bLength
    USB_DT_ENDPOINT,                            // bDescriptorType
    USB_ENDPOINT_IN | 1,                        // bEndpointAddress
    USB_ENDPOINT_INTERRUPT,                     // bmAttributes
    4, 0,                                       // wMaxPacketSize
    12,                                         // bInterval
};

static inline bool is_roothub_request(usb_dwc_transfer_request_t* req) {
    iotxn_t* txn = req->txn;
    usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);
    return data->device_id == ROOT_HUB_DEVICE_ID;
}

static inline bool is_control_request(usb_dwc_transfer_request_t* req) {
    iotxn_t* txn = req->txn;
    usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);
    return data->ep_address == 0;
}

// Completes the iotxn associated with a request then cleans up the request.
static void complete_request(
    usb_dwc_transfer_request_t* req,
    mx_status_t status,
    size_t length
) {
    iotxn_t* txn = req->txn;
    txn->ops->complete(txn, status, length);

    // TODO(gkalsi): Just for diagnostics.
    memset(req, 0xC, sizeof(*req));

    free(req);
}

static void dwc_complete_root_port_status_txn(void) {

    mtx_lock(&rh_status_mtx);

    if (root_port_status.wPortChange) {
        if (rh_intr_req && rh_intr_req->txn) {
            iotxn_t* txn = rh_intr_req->txn;
            uint16_t val = 0x2;
            txn->ops->copyto(txn, (void*)&val, sizeof(val), 0);
            complete_request(rh_intr_req, NO_ERROR, sizeof(val));
            rh_intr_req = NULL;
        }
    }
    mtx_unlock(&rh_status_mtx);
}

static void dwc_reset_host_port(void) {
    union dwc_host_port_ctrlstatus hw_status = regs->host_port_ctrlstatus;
    hw_status.enabled = 0;
    hw_status.connected_changed = 0;
    hw_status.enabled_changed = 0;
    hw_status.overcurrent_changed = 0;

    hw_status.reset = 1;
    regs->host_port_ctrlstatus = hw_status;

    mx_nanosleep(MX_MSEC(60));
    
    hw_status.reset = 0;
    regs->host_port_ctrlstatus = hw_status;
}

static void dwc_host_port_power_on(void)
{
    union dwc_host_port_ctrlstatus hw_status = regs->host_port_ctrlstatus;
    hw_status.enabled = 0;
    hw_status.connected_changed = 0;
    hw_status.enabled_changed = 0;
    hw_status.overcurrent_changed = 0;

    hw_status.powered = 1;
    regs->host_port_ctrlstatus = hw_status;
}

static mx_status_t usb_dwc_softreset_core(void) {
    while (!(regs->core_reset & DWC_AHB_MASTER_IDLE));

    regs->core_reset = DWC_SOFT_RESET;
    while (regs->core_reset & DWC_SOFT_RESET);

    return NO_ERROR;
}

static mx_status_t usb_dwc_setupcontroller(void) {
    const uint32_t rx_words = 1024;
    const uint32_t tx_words = 1024;
    const uint32_t ptx_words = 1024;

    regs->rx_fifo_size = rx_words;
    regs->nonperiodic_tx_fifo_size = (tx_words << 16) | rx_words;
    regs->host_periodic_tx_fifo_size = (ptx_words << 16) | (rx_words + tx_words);

    regs->ahb_configuration |= DWC_AHB_DMA_ENABLE | BCM_DWC_AHB_AXI_WAIT;

    union dwc_core_interrupts core_interrupt_mask;

    regs->core_interrupt_mask.val = 0;
    regs->core_interrupts.val = 0xffffffff;

    core_interrupt_mask.val = 0;
    core_interrupt_mask.host_channel_intr = 1;
    core_interrupt_mask.port_intr = 1;
    regs->core_interrupt_mask = core_interrupt_mask;

    regs->ahb_configuration |= DWC_AHB_INTERRUPT_ENABLE;

    return NO_ERROR;
}

// Queue a transaction on the DWC root hub.
static void dwc_iotxn_queue_rh(usb_dwc_t* dwc, 
                               usb_dwc_transfer_request_t* req) {
    mtx_lock(&dwc->rh_txn_mtx);

    list_add_tail(&dwc->rh_txn_head, &req->node);

    mtx_unlock(&dwc->rh_txn_mtx);

    // Signal to the processor thread to wake up and process this request.
    completion_signal(&dwc->rh_txn_completion);
}

// Queue a transaction on external peripherals using the DWC host channels.
static void dwc_iotxn_queue_hw(usb_dwc_t* dwc,
                               usb_dwc_transfer_request_t* req) {
    
}

static void do_dwc_iotxn_queue(usb_dwc_t* dwc, iotxn_t* txn) {
    // Once an iotxn enters the low-level DWC stack, it is always encapsulated
    // by a usb_dwc_transfer_request_t.
    usb_dwc_transfer_request_t* req = calloc(1, sizeof(*req));
    if (!req) {
        // If we can't allocate memory for the request, complete the iotxn with
        // a failure.
        txn->ops->complete(txn, ERR_NO_MEMORY, 0);
        return;
    }

    // Initialize the request.
    req->txn = txn;
    req->bMaxPacketSize0 = 8;

    if (is_roothub_request(req)) {
        dwc_iotxn_queue_rh(dwc, req);
    } else {
        dwc_iotxn_queue_hw(dwc, req);
    }
}

static void dwc_iotxn_queue(mx_device_t* hci_device, iotxn_t* txn) {
    usb_dwc_t* dwc = dev_to_usb_dwc(hci_device);
    do_dwc_iotxn_queue(dwc, txn);
}

static void dwc_unbind(mx_device_t* dev) {
    xprintf("usb dwc_unbind not implemented\n");
}

static mx_status_t dwc_release(mx_device_t* device) {
    xprintf("usb dwc_release not implemented\n");
    return NO_ERROR;
}


static mx_protocol_device_t dwc_device_proto = {
    .iotxn_queue = dwc_iotxn_queue,
    .unbind = dwc_unbind,
    .release = dwc_release,
};


static void dwc_set_bus_device(mx_device_t* device, mx_device_t* busdev) {
    usb_dwc_t* dwc = dev_to_usb_dwc(device);
    dwc->bus_device = busdev;
    if (busdev) {
        device_get_protocol(busdev, MX_PROTOCOL_USB_BUS,
                            (void**)&dwc->bus_protocol);
        dwc_reset_host_port();
        dwc->bus_protocol->add_device(dwc->bus_device, ROOT_HUB_DEVICE_ID, 0,
                                      USB_SPEED_HIGH);
    } else {
        dwc->bus_protocol = NULL;
    }
}

static size_t dwc_get_max_device_count(mx_device_t* device) {
    return MAX_DEVICE_COUNT;
}

static mx_status_t dwc_enable_ep(mx_device_t* hci_device, uint32_t device_id,
                                  usb_endpoint_descriptor_t* ep_desc, bool enable) {
    xprintf("usb dwc_enable_ep not implemented\n");
    return NO_ERROR;
}

static uint64_t dwc_get_frame(mx_device_t* hci_device) {
    xprintf("usb dwc_get_frame not implemented\n");
    return NO_ERROR;
}

mx_status_t dwc_config_hub(mx_device_t* hci_device, uint32_t device_id, usb_speed_t speed,
                            usb_hub_descriptor_t* descriptor) {
    xprintf("usb dwc_config_hub not implemented\n");
    return NO_ERROR;
}

mx_status_t dwc_hub_device_added(mx_device_t* hci_device, uint32_t hub_address, int port,
                                  usb_speed_t speed) {
    xprintf("usb dwc_hub_device_added not implemented\n");


    return NO_ERROR;
}
mx_status_t dwc_hub_device_removed(mx_device_t* hci_device, uint32_t hub_address, int port) {
    xprintf("usb dwc_hub_device_removed not implemented\n");
    return NO_ERROR;
}


static usb_hci_protocol_t dwc_hci_protocol = {
    .set_bus_device = dwc_set_bus_device,
    .get_max_device_count = dwc_get_max_device_count,
    .enable_endpoint = dwc_enable_ep,
    .get_current_frame = dwc_get_frame,
    .configure_hub = dwc_config_hub,
    .hub_device_added = dwc_hub_device_added,
    .hub_device_removed = dwc_hub_device_removed,
};

void dwc_handle_irq(void) {
    union dwc_core_interrupts interrupts = regs->core_interrupts;

    if (interrupts.port_intr) {
        // Clear the interrupt.
        union dwc_host_port_ctrlstatus hw_status = regs->host_port_ctrlstatus;

        mtx_lock(&rh_status_mtx);

        root_port_status.wPortChange = 0;
        root_port_status.wPortStatus = 0;

        // This device only has one port.
        if (hw_status.connected)
            root_port_status.wPortStatus |= USB_PORT_CONNECTION;
        if (hw_status.enabled)
            root_port_status.wPortStatus |= USB_PORT_ENABLE;
        if (hw_status.suspended)
            root_port_status.wPortStatus |= USB_PORT_SUSPEND;
        if (hw_status.overcurrent)
            root_port_status.wPortStatus |= USB_PORT_OVER_CURRENT;
        if (hw_status.reset)
            root_port_status.wPortStatus |= USB_PORT_RESET;
        if (hw_status.speed == USB_SPEED_LOW)
            root_port_status.wPortStatus |= USB_PORT_LOW_SPEED;
        if (hw_status.speed == USB_SPEED_HIGH)
            root_port_status.wPortStatus |= USB_PORT_HIGH_SPEED;

        if (hw_status.connected_changed)
            root_port_status.wPortChange |= USB_PORT_CONNECTION;
        if (hw_status.enabled_changed)
            root_port_status.wPortChange |= USB_PORT_ENABLE;
        if (hw_status.overcurrent_changed)
            root_port_status.wPortChange |= USB_PORT_OVER_CURRENT;

        mtx_unlock(&rh_status_mtx);

        // Clear the interrupt.
        hw_status.enabled = 0;
        regs->host_port_ctrlstatus = hw_status;

        dwc_complete_root_port_status_txn();

    }
}

// Thread to handle interrupts.
static int dwc_irq_thread(void* arg) {
    usb_dwc_t* dwc = (usb_dwc_t*)arg;

    device_add(&dwc->device, dwc->parent);
    dwc->parent = NULL;

    while (1) {
        mx_status_t wait_res;

        wait_res = mx_handle_wait_one(dwc->irq_handle, MX_SIGNAL_SIGNALED,
                                      MX_TIME_INFINITE, NULL);
        if (wait_res != NO_ERROR)
            printf("dwc_irq_thread::mx_handle_wait_one(irq_handle) returned "
                   "error code = %d\n", wait_res);

        dwc_handle_irq();

        mx_interrupt_complete(dwc->irq_handle);
    }

    printf("dwc_irq_thread done.\n");
    return 0;
}

static mx_status_t dwc_host_port_set_feature(uint16_t feature)
{
    if (feature == USB_FEATURE_PORT_POWER) {
        dwc_host_port_power_on();
        return NO_ERROR;
    } else if (feature == USB_FEATURE_PORT_RESET) {
        dwc_reset_host_port();
        return NO_ERROR;
    }

    return ERR_NOT_SUPPORTED;
}

static void dwc_root_hub_get_descriptor(usb_dwc_transfer_request_t* req) {
    iotxn_t* txn = req->txn;
    usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);
    usb_setup_t* setup = &data->setup;

    uint16_t value = le16toh(setup->wValue);
    uint16_t index = le16toh(setup->wIndex);
    uint16_t length = le16toh(setup->wLength);

    uint8_t desc_type = value >> 8;
    if (desc_type == USB_DT_DEVICE && index == 0) {
        if (length > sizeof(usb_device_descriptor_t)) length = sizeof(usb_device_descriptor_t);
        txn->ops->copyto(txn, dwc_rh_descriptor, length, 0);
        complete_request(req, NO_ERROR, length);
    } else if (desc_type == USB_DT_CONFIG && index == 0) {
        usb_configuration_descriptor_t* config_desc =
            (usb_configuration_descriptor_t*)dwc_rh_config_descriptor;
        uint16_t desc_length = le16toh(config_desc->wTotalLength);
        if (length > desc_length) length = desc_length;
        txn->ops->copyto(txn, dwc_rh_config_descriptor, length, 0);
        complete_request(req, NO_ERROR, length);
    } else if (value >> 8 == USB_DT_STRING) {
        uint8_t string_index = value & 0xFF;
        if (string_index < countof(dwc_rh_string_table)) {
            const uint8_t* string = dwc_rh_string_table[string_index];
            if (length > string[0]) length = string[0];

            txn->ops->copyto(txn, string, length, 0);
            complete_request(req, NO_ERROR, length);
        } else {
            complete_request(req, ERR_NOT_SUPPORTED, 0);
        }
    }
}

static void dwc_process_root_hub_std_req(usb_dwc_transfer_request_t* req) {
    iotxn_t* txn = req->txn;
    usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);
    usb_setup_t* setup = &data->setup;

    uint8_t request = setup->bRequest;

    if (request == USB_REQ_SET_ADDRESS) {
        complete_request(req, NO_ERROR, 0);
    } else if (request == USB_REQ_GET_DESCRIPTOR) {
        dwc_root_hub_get_descriptor(req);
    } else if (request == USB_REQ_SET_CONFIGURATION) {
        complete_request(req, NO_ERROR, 0);
    } else {
        complete_request(req, ERR_NOT_SUPPORTED, 0);
    }
}

static void dwc_process_root_hub_class_req(usb_dwc_transfer_request_t* req) {
    iotxn_t* txn = req->txn;
    usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);
    usb_setup_t* setup = &data->setup;

    uint8_t request = setup->bRequest;
    uint16_t value = le16toh(setup->wValue);
    uint16_t index = le16toh(setup->wIndex);
    uint16_t length = le16toh(setup->wLength);

    if (request == USB_REQ_GET_DESCRIPTOR) {
        if (value == USB_HUB_DESC_TYPE << 8 && index == 0) {
            usb_hub_descriptor_t desc;
            memset(&desc, 0, sizeof(desc));
            desc.bDescLength = sizeof(desc);
            desc.bDescriptorType = value >> 8;
            desc.bNbrPorts = 1;
            desc.bPowerOn2PwrGood = 0;

            if (length > sizeof(desc)) length = sizeof(desc);
            txn->ops->copyto(txn, &desc, length, 0);
            complete_request(req, NO_ERROR, length);
            return;
        }
    } else if (request == USB_REQ_SET_FEATURE) {
        mx_status_t res = dwc_host_port_set_feature(value);
        complete_request(req, res, 0);
    } else if (request == USB_REQ_CLEAR_FEATURE) {
        mtx_lock(&rh_status_mtx);
        uint16_t* change_bits = &(root_port_status.wPortChange);
        switch (value) {
            case USB_FEATURE_C_PORT_CONNECTION:
                *change_bits &= ~USB_PORT_CONNECTION;
                break;
            case USB_FEATURE_C_PORT_ENABLE:
                *change_bits &= ~USB_PORT_ENABLE;
                break;
            case USB_FEATURE_C_PORT_SUSPEND:
                *change_bits &= ~USB_PORT_SUSPEND;
                break;
            case USB_FEATURE_C_PORT_OVER_CURRENT:
                *change_bits &= ~USB_PORT_OVER_CURRENT;
                break;
            case USB_FEATURE_C_PORT_RESET:
                *change_bits &= ~USB_PORT_RESET;
                break;
        }
        mtx_unlock(&rh_status_mtx);
        complete_request(req, NO_ERROR, 0);
    } else if (request == USB_REQ_GET_STATUS) {
        size_t length = txn->length;
        if (length > sizeof(root_port_status)) {
            length = sizeof(root_port_status);
        }
        
        mtx_lock(&rh_status_mtx);
        txn->ops->copyto(txn, &root_port_status, length, 0);
        mtx_unlock(&rh_status_mtx);
        
        complete_request(req, NO_ERROR, length);
    } else {
        complete_request(req, ERR_NOT_SUPPORTED, 0);
    }
}

static void dwc_process_root_hub_ctrl_req(usb_dwc_transfer_request_t* req) {
    iotxn_t* txn = req->txn;
    usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);
    usb_setup_t* setup = &data->setup;

    if ((setup->bmRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
        dwc_process_root_hub_std_req(req);
    } else if ((setup->bmRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS) {
        dwc_process_root_hub_class_req(req);
    } else {
        // Some unknown request type?
        assert(false);
    }
}

static void dwc_process_root_hub_request(usb_dwc_t* dwc,
                                         usb_dwc_transfer_request_t* req) {
    assert(req);

    if (is_control_request(req)) {
        dwc_process_root_hub_ctrl_req(req);
    } else {
        mtx_lock(&rh_status_mtx);
        rh_intr_req = req;
        mtx_unlock(&rh_status_mtx);

        dwc_complete_root_port_status_txn();

    }
}

// Thread to handle queues transactions on the root hub.
static int dwc_root_hub_txn_worker(void* arg) {
    usb_dwc_t* dwc = (usb_dwc_t*)arg;

    dwc->rh_txn_completion = COMPLETION_INIT;

    while (true) {
        completion_wait(&dwc->rh_txn_completion, MX_TIME_INFINITE);

        mtx_lock(&dwc->rh_txn_mtx);

        usb_dwc_transfer_request_t* req =
            list_remove_head_type(&dwc->rh_txn_head, 
                                  usb_dwc_transfer_request_t, node);

        if (list_is_empty(&dwc->rh_txn_head)) {
            completion_reset(&dwc->rh_txn_completion);
        }

        mtx_unlock(&dwc->rh_txn_mtx);

        dwc_process_root_hub_request(dwc, req);
    }

    return -1;
}

static int dwc_channel_worker_thread(void* arg) {
    dwc_channel_context_t* self = (dwc_channel_context_t*)arg;
    while (1) {
        completion_wait(&self->request_ready_completion, MX_TIME_INFINITE);
        completion_reset(&self->request_ready_completion);

        if (!self->active_request) {
            printf("WARNING - Channel Worker Thread %d woken with no work to"
                   "do!\n", self->channel_id);
            continue;
        }



        printf("Request ready on channel %u\n", self->channel_id);
    }

    return -1;
}


static int dwc_channel_scheduler_thread(void *arg) {

    while (true) {
        // Wait for somebody to tell us to schedule a transfer.
        completion_wait(&pending_transfer_completion, MX_TIME_INFINITE);

        // Wait for a channel to become available.
        uint channel = acquire_channel_blocking();

        mtx_lock(&pending_transfer_mtx);
        usb_dwc_transfer_request_t* req =
            list_remove_head_type(&pending_transfer_list, usb_dwc_transfer_request_t, 
                                  node);

        if (list_is_empty(&pending_transfer_list)) {
            completion_reset(&pending_transfer_completion);
        }

        mtx_unlock(&pending_transfer_mtx);

        if (!req) {
            printf("WARNING - channel scheduler thread woken up with no work "
                   "to do!\n");
            continue;
        }

        // Assign this request to a channel and signal the channel's thread to 
        // execute the request.
        dwc_channel_context_t* ch = &channel_context[channel];
        
        ch->active_request = req;
        completion_signal(&ch->request_ready_completion);
    }

    return -1;
}

static uint acquire_channel_blocking(void) {
    int next_channel = -1;

    while (true) {
        mtx_lock(&free_channel_mtx);

        // A quick sanity check. We should never mark a channel that doesn't 
        // exist on the system as free.
        assert((free_channels & ALL_CHANNELS_FREE) == free_channels);

        // Is there at least one channel that's free?
        next_channel = -1;
        if (free_channels) {
            next_channel = __builtin_ctz(free_channels);
            
            // Mark the bit in the free_channel bitfield = 0, meaning the
            // channel is in use.
            free_channels &= (ALL_CHANNELS_FREE ^ (1 << next_channel)); 
        }

        if (next_channel == -1) {
            completion_reset(&free_channel_completion);
        }

        mtx_unlock(&free_channel_mtx);

        if (next_channel >= 0) {
            return next_channel;
        }

        // We couldn't find a free channel, wait for somebody to tell us to 
        // wake up and attempt to acquire a channel again.
        completion_wait(&free_channel_completion, MX_TIME_INFINITE);
    }

    __UNREACHABLE;
}

static void release_channel(uint ch) {
    assert(ch < DWC_NUM_CHANNELS);

    mtx_lock(&free_channel_mtx);

    free_channels |= (1 << ch);

    mtx_unlock(&free_channel_mtx);

    completion_signal(&free_channel_completion);
}

// Bind is the entry point for this driver.
static mx_status_t usb_dwc_bind(mx_driver_t* drv, mx_device_t* dev) {
    xprintf("usb_dwc_bind drv = %p, dev = %p\n", drv, dev);

    usb_dwc_t *usb_dwc = NULL; 
    mx_handle_t irq_handle = MX_HANDLE_INVALID;
    mx_status_t st = ERR_INTERNAL;

    // Allocate a new device object for the bus.
    usb_dwc = calloc(1, sizeof(*usb_dwc));
    if (!usb_dwc) {
        xprintf("usb_dwc_bind failed to allocated usb_dwc struct.\n");
        return ERR_NO_MEMORY;
    }

    // Carve out some address space for this device.
    st = mx_mmap_device_memory(
        get_root_resource(), USB_PAGE_START, (uint32_t)USB_PAGE_SIZE, 
        MX_CACHE_POLICY_UNCACHED_DEVICE, (void*)(&regs)
    );
    if (st != NO_ERROR) {
        xprintf("usb_dwc_bind failed to mx_mmap_device_memory.\n");
        goto error_return;
    }

    // Create an IRQ Handle for this device.
    irq_handle = mx_interrupt_create(get_root_resource(), INTERRUPT_VC_USB, 
                                     MX_FLAG_REMAP_IRQ);
    if (irq_handle < 0) {
        xprintf("usb_dwc_bind failed to map usb irq.\n");
        st = ERR_NO_RESOURCES;
        goto error_return;
    }

    usb_dwc->irq_handle = irq_handle;
    usb_dwc->parent = dev;
    list_initialize(&usb_dwc->rh_txn_head);

    list_initialize(&pending_transfer_list);
    
    // TODO(gkalsi):
    // The BCM Mailbox Driver currently turns on USB power but it should be 
    // done here instead.

    if ((st = usb_dwc_softreset_core()) != NO_ERROR) {
        xprintf("usb_dwc_bind failed to reset core.\n");
        goto error_return;
    }

    if ((st = usb_dwc_setupcontroller()) != NO_ERROR) {
        xprintf("usb_dwc_bind failed setup controller.\n");
        goto error_return;
    }

    device_init(&usb_dwc->device, drv, "bcm-usb-dwc", &dwc_device_proto);

    usb_dwc->device.protocol_id = MX_PROTOCOL_USB_HCI;
    usb_dwc->device.protocol_ops = &dwc_hci_protocol;

    // Thread that responds to requests for the root hub.
    thrd_t root_hub_txn_worker;
    thrd_create_with_name(&root_hub_txn_worker, dwc_root_hub_txn_worker,
                          usb_dwc, "dwc_root_hub_txn_worker");
    thrd_detach(root_hub_txn_worker);

    for (size_t i = 0; i < NUM_HOST_CHANNELS; i++) {
        channel_context[i].channel_id = i;
        channel_context[i].active_request = NULL;

        channel_context[i].request_ready_completion = COMPLETION_INIT;
        channel_context[i].transaction_finished_completion = COMPLETION_INIT;

        thrd_create(
            &channel_context[i].channel_thread,
            dwc_channel_worker_thread,
            &channel_context[i]
        );

        thrd_detach(channel_context[i].channel_thread);
    }

    thrd_t irq_thread;
    thrd_create_with_name(&irq_thread, dwc_irq_thread, usb_dwc,
                          "dwc_irq_thread");
    thrd_detach(irq_thread);

    completion_signal(&channel_context[0].request_ready_completion);

    xprintf("usb_dwc_bind success!\n"); 
    return NO_ERROR;
error_return:
    if (usb_dwc)
        free(usb_dwc);

    return st;
}


mx_driver_t _driver_usb_dwc = {
    .ops = {
        .bind = usb_dwc_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_usb_dwc, "bcm-usb-dwc", "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_SOC_VID, SOC_VID_BROADCOMM),
    BI_MATCH_IF(EQ, BIND_SOC_DID, SOC_DID_BROADCOMM_MAILBOX),
MAGENTA_DRIVER_END(_driver_usb_dwc)