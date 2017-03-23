// Copyright 2017 The Fuchsia Authors. All riusghts reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb-client.h>
#include <ddk/protocol/usb.h>

#include <magenta/types.h>
#include <magenta/device/usb-client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "usb-virtual-bus.h"

typedef struct usb_virtual_client {
    // the device we implement
    mx_device_t device;
    mx_handle_t channel_handle;
    usb_client_callbacks_t* callbacks;
    void* callbacks_cookie;
} usb_virtual_client_t;
#define dev_to_usb_virtual_client(dev) containerof(dev, usb_virtual_client_t, device)

static void handle_packet(usb_virtual_client_t* client, usb_virt_header_t* header) {
printf("handle_packet length %zu\n", header->data_length);
        char    response_buffer[USB_VIRT_BUFFER_SIZE];
        usb_virt_header_t* response = (usb_virt_header_t *)response_buffer;

    if (header->ep_addr == 0 && header->data_length >= sizeof(usb_setup_t)) {
        mx_status_t status;

        if (client->callbacks) {
            usb_setup_t* setup = (usb_setup_t *)header->data;

    printf("handle_packet type: 0x%02X req: %d value: %d index: %d length: %d\n",
            setup->bmRequestType, setup->bRequest, le16toh(setup->wValue), le16toh(setup->wIndex), le16toh(setup->wLength));
            
            void* buffer;
            size_t length;
            if ((setup->bmRequestType & USB_DIR_MASK) == USB_DIR_IN) {
                buffer = response->data;
                length = sizeof(response_buffer) - sizeof(usb_virt_header_t);
            } else {
                buffer = setup + 1;
                length = header->data_length - sizeof(*setup);
            }
            status = client->callbacks->control(setup, buffer, length, client->callbacks_cookie);
            printf("control returned %d\n", status);
        } else {
            status = ERR_UNAVAILABLE;
        }

        // send response
        printf("status %d write response\n", status);
        response->cmd = USB_VIRT_PACKET_RESP;
        response->cookie = header->cookie;
        response->status = (status > 0 ? NO_ERROR : status);
        response->data_length = (status < 0 ? 0 : status);
        size_t packet_length = sizeof(usb_virt_header_t);
        if (status > 0) packet_length += status;
        mx_channel_write(client->channel_handle, 0, response, packet_length, NULL, 0);
    } else {
        printf("non ep0 not supported yet\n");
    }
}

static void usb_virtual_client_set_callbacks(mx_device_t* dev, usb_client_callbacks_t* callbacks,
                                             void* cookie) {
printf("usb_virtual_client_set_callbacks\n");
    usb_virtual_client_t* client = dev_to_usb_virtual_client(dev);
    client->callbacks = callbacks;
    client->callbacks_cookie = cookie;
}

static mx_status_t usb_virtual_client_config_ep(mx_device_t* dev, const usb_endpoint_descriptor_t* ep_desc) {
//    usb_virtual_client_t* client = dev_to_usb_virtual_client(dev);
    return NO_ERROR;
}

static void usb_virtual_client_set_connected(usb_virtual_client_t* client, bool connected) {
    usb_virt_header_t connect;
    connect.cmd = (connected ? USB_VIRT_CONNECT : USB_VIRT_DISCONNECT);
    mx_channel_write(client->channel_handle, 0, &connect, sizeof(connect), NULL, 0);
}

usb_client_protocol_t virtual_client_protocol = {
    .set_callbacks = usb_virtual_client_set_callbacks,
    .config_ep = usb_virtual_client_config_ep,
};

static mx_status_t usb_virtual_client_open(mx_device_t* dev, mx_device_t** dev_out, uint32_t flags) {
printf("usb_virtual_client_open\n");
    return NO_ERROR;
}

static ssize_t usb_virtual_client_ioctl(mx_device_t* dev, uint32_t op,
        const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    usb_virtual_client_t* client = dev_to_usb_virtual_client(dev);

    switch (op) {
    case IOCTL_USB_CLIENT_SET_CONNNECTED: {
        if (!in_buf || in_len != sizeof(int)) {
            return ERR_INVALID_ARGS;
        }
        int connected = *((int *)in_buf);
        printf("IOCTL_USB_CLIENT_SET_CONNNECTED %d\n", connected);
        usb_virtual_client_set_connected(client, !!connected);
        return NO_ERROR;
    }
    }
    return ERR_NOT_SUPPORTED;
}

static void usb_virtual_client_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
}

static void usb_virtual_client_unbind(mx_device_t* dev) {
    printf("usb_virtual_client_unbind\n");
//    usb_virtual_client_t* client = dev_to_usb_virtual_client(dev);

}

static mx_status_t usb_virtual_client_release(mx_device_t* device) {
    // FIXME - do something here
    return NO_ERROR;
}

static mx_protocol_device_t usb_virtual_client_device_proto = {
    .open = usb_virtual_client_open,
    .ioctl = usb_virtual_client_ioctl,
    .iotxn_queue = usb_virtual_client_iotxn_queue,
    .unbind = usb_virtual_client_unbind,
    .release = usb_virtual_client_release,
};


static int usb_virtual_client_thread(void* arg) {
    usb_virtual_client_t* client = (usb_virtual_client_t*)arg;

printf("usb_virtual_client_thread\n");
    while (1) {
        char    buffer[USB_VIRT_BUFFER_SIZE];
        uint32_t actual;
        
        mx_status_t status = mx_object_wait_one(client->channel_handle, MX_CHANNEL_READABLE, MX_TIME_INFINITE, NULL);
printf("mx_object_wait_one returned %d\n", status);
        status = mx_channel_read(client->channel_handle, 0, buffer, sizeof(buffer),
                                             &actual, NULL, 0, NULL);
        if (status != NO_ERROR) {
            printf("usb_virtual_client_thread mx_channel_read failed %d\n", status);
            return status;
        }

        usb_virt_header_t* header = (usb_virt_header_t *)buffer;
        switch (header->cmd) {
        case USB_VIRT_PACKET:
printf("client got packet\n");
            handle_packet(client, header);
            break;
        default:
            printf("usb_virtual_client_thread bad command %d\n", header->cmd);
            break;
        }
    }

    return 0;
}

static mx_status_t usb_virtual_client_init(mx_driver_t* drv) {
printf("usb_virtual_client_init\n");
    usb_virtual_client_t* client = calloc(1, sizeof(usb_virtual_client_t));
    if (!client) {
        return ERR_NO_MEMORY;
    }

    client->channel_handle = usb_virt_get_client_channel();
    if (client->channel_handle == MX_HANDLE_INVALID) {
        printf("usb_virt_get_client_channel failed\n");
        return -1;
    }

    device_init(&client->device, drv, "usb-virtual-client", &usb_virtual_client_device_proto);

    thrd_t thread;
    thrd_create_with_name(&thread, usb_virtual_client_thread, client, "usb_virtual_client_thread");
    thrd_detach(thread);

    client->device.protocol_id = MX_PROTOCOL_USB_CLIENT;
    client->device.protocol_ops = &virtual_client_protocol;

    mx_status_t status = device_add(&client->device, driver_get_root_device());
    if (status != NO_ERROR) {
        free(client);
    }
    return status;
}

mx_driver_t _driver_usb_virtual_client = {
    .ops = {
        .init = usb_virtual_client_init,
    },
};

// clang-format off
MAGENTA_DRIVER_BEGIN(_driver_usb_virtual_client, "usb-virtual-client", "magenta", "0.1", 0)
MAGENTA_DRIVER_END(_driver_usb_virtual_client)
