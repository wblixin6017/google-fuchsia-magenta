// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>
#include <string.h>
#include <stdio.h>

#include "usb-audio-txring.h"

mx_status_t usb_audio_txring_init(usb_audio_txring_t* txring) {
    memset(txring, 0, sizeof(*txring));
    return mx_event_create(0, &txring->stop_event);
}

mx_status_t usb_audio_txring_ioctl(usb_audio_txring_t* txring, uint32_t op, const void* in_buf,
                                   size_t in_len, void* out_buf, size_t out_len) {
    switch (op) {
    case IOCTL_AUDIO_SET_BUFFER: {
        if (in_len != sizeof(mx_handle_t)) return ERR_INVALID_ARGS;
        mx_handle_t handle = *((mx_handle_t *)in_buf);
        mx_status_t status = mx_vmo_get_size(handle, &txring->buffer_size);
        if (status != NO_ERROR) {
            mx_handle_close(handle);
            return status;
        }
        // clean up existing buffer
        if (txring->buffer_vmo != MX_HANDLE_INVALID) {
            mx_process_unmap_vm(txring->buffer_vmo, (uintptr_t)txring->buffer, txring->buffer_size);

            mx_handle_close(txring->buffer_vmo);
            txring->buffer_vmo = MX_HANDLE_INVALID;
            txring->buffer = NULL;
        }
        status = mx_process_map_vm(mx_process_self(), handle, 0, txring->buffer_size,
                                   (uintptr_t *)&txring->buffer,
                                   MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
        if (status != NO_ERROR) {
            mx_handle_close(handle);
            return status;
        }
        txring->buffer_vmo = handle;
        return NO_ERROR;
    }
    case IOCTL_AUDIO_SET_TXRING: {
        if (in_len != sizeof(mx_audio_set_txring_args_t)) return ERR_INVALID_ARGS;
        mx_audio_set_txring_args_t* args = (mx_audio_set_txring_args_t *)in_buf;
        // clean up existing buffer
        if (txring->txring_vmo != MX_HANDLE_INVALID) {
            mx_process_unmap_vm(txring->txring_vmo, (uintptr_t)txring->ring,
                            sizeof(mx_audio_txring_entry_t) * txring->txring_count);
            mx_handle_close(txring->txring_vmo);
            txring->txring_vmo = MX_HANDLE_INVALID;
            txring->ring = NULL;
        }
        if (!args->count || (args->count & (args->count - 1))) {
            printf("count must be a power of two in IOCTL_AUDIO_SET_TXRING\n");
            mx_handle_close(args->txring);
            return ERR_INVALID_ARGS;
        }
        mx_status_t status = mx_process_map_vm(mx_process_self(), args->txring, 0,
                                   sizeof(*txring->ring) * args->count,
                                   (uintptr_t *)&txring->ring,
                                   MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
        if (status != NO_ERROR) {
            mx_handle_close(args->txring);
            return status;
        }
        txring->txring_vmo = args->txring;
        txring->txring_count = args->count;
        return NO_ERROR;
    }
    case IOCTL_AUDIO_GET_FIFO:
        if (out_len != sizeof(mx_handle_t)) return ERR_INVALID_ARGS;
        if (txring->fifo_handle == MX_HANDLE_INVALID) {
            if (txring->txring_count == 0) return ERR_BAD_STATE;
            mx_status_t status = mx_fifo_create(txring->txring_count, &txring->fifo_handle);
            if (status != NO_ERROR) return status;
        }
        txring->fifo_state.head = txring->fifo_state.tail = 0;
        return mx_handle_duplicate(txring->fifo_handle, MX_FIFO_PRODUCER_RIGHTS,
                                   (mx_handle_t *)out_buf);
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_status_t usb_audio_txring_wait(usb_audio_txring_t* txring, void** out_buffer, uint32_t* out_len) {

    while (txring->fifo_state.head == txring->fifo_state.tail) {
        mx_wait_item_t items[2];
        items[0].waitfor = MX_FIFO_NOT_EMPTY;
        items[0].handle = txring->fifo_handle;
        items[1].waitfor = MX_EVENT_SIGNALED;
        items[1].handle = txring->stop_event;
        
printf("usb_audio_txring_wait mx_handle_wait_many\n");
        mx_status_t status = mx_handle_wait_many(items, countof(items), MX_TIME_INFINITE);
printf("mx_handle_wait_many returned\n");
        if (status < 0) {
            printf("mx_handle_wait_many failed: %d\n", status);
            return status;
        }
       if (items[1].pending & MX_EVENT_SIGNALED) return ERR_REMOTE_CLOSED;

        status = mx_fifo_op(txring->fifo_handle, MX_FIFO_OP_READ_STATE, 0, &txring->fifo_state);
        if (status < 0) {
            printf("mx_fifo_op failed to read state: %d\n", status);
            return status;
        }
    }

    mx_audio_txring_entry_t* entry = &txring->ring[txring->txring_index];
    *out_buffer = &txring->buffer[entry->data_offset];
    *out_len = entry->data_size;
    return NO_ERROR;
}

static mx_status_t usb_audio_txring_complete(usb_audio_txring_t* txring, mx_status_t status) {
printf("usb_audio_txring_complete\n");
    mx_audio_txring_entry_t* entry = &txring->ring[txring->txring_index];
    txring->txring_index++;
    if (txring->txring_index == txring->txring_count) txring->txring_index = 0;
    entry->status = status;

    return mx_fifo_op(txring->fifo_handle, MX_FIFO_OP_ADVANCE_TAIL, 1, &txring->fifo_state);
}

static int usb_audio_txring_thread(void* arg) {
    usb_audio_txring_t* txring = (usb_audio_txring_t *)arg;

    while (1) {
        void* data;
        uint32_t length;

        mx_status_t status = usb_audio_txring_wait(txring, &data, &length);
        if (status < 0) {
            printf("usb_audio_txring_wait failed: %d\n", status);
            return status;
        }

        mx_status_t ret = txring->callback(data, length, txring->cookie);
         if (ret < 0) {
            printf("txring->callback failed: %d\n", ret);
            return status;
        }

        status = usb_audio_txring_complete(txring, ret);
        if (status < 0) {
            printf("usb_audio_txring_complete failed: %d\n", status);
            return status;
        }
    }

printf("usb_audio_txring_thread done\n");

    return 0;
}

void usb_audio_txring_start(usb_audio_txring_t* txring, txring_callback callback, void* cookie) {
printf("usb_audio_txring_start\n");
    // clear our stop_event signal
    mx_object_signal(txring->stop_event, MX_EVENT_SIGNALED, 0);

    txring->callback = callback;
    txring->cookie = cookie;
    thrd_create_with_name(&txring->thread, usb_audio_txring_thread, txring, "usb_audio_txring_thread");
}

void usb_audio_txring_stop(usb_audio_txring_t* txring) {
printf("usb_audio_txring_stop\n");
    // wait for transactions to complete
    mx_handle_wait_one(txring->fifo_handle, MX_FIFO_EMPTY, MX_TIME_INFINITE, NULL);

printf("signal stop_event\n");
    mx_status_t status = mx_object_signal(txring->stop_event, 0, MX_EVENT_SIGNALED);
printf("mx_object_signal returned %d\n", status);
    
    thrd_join(txring->thread, NULL);
printf("usb_audio_txring_stop done\n");
}

void usb_audio_txring_release(usb_audio_txring_t* txring) {
printf("usb_audio_txring_release\n");
    if (txring->buffer) {
        mx_process_unmap_vm(txring->buffer_vmo, (uintptr_t)txring->buffer, txring->buffer_size);
    }
    if (txring->ring) {
        mx_process_unmap_vm(txring->txring_vmo, (uintptr_t)txring->ring,
                            sizeof(mx_audio_txring_entry_t) * txring->txring_count);
    }

    mx_handle_close(txring->buffer_vmo);
    mx_handle_close(txring->txring_vmo);
    mx_handle_close(txring->fifo_handle);
    mx_handle_close(txring->stop_event);
    memset(txring, 0, sizeof(*txring));
}
