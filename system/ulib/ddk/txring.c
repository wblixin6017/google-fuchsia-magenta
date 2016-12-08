// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/driver.h>
#include <ddk/txring.h>
#include <magenta/syscalls.h>
#include <magenta/device/txring.h>

#include <string.h>
#include <stdio.h>

mx_status_t txring_init(txring_t* txring, uint32_t buffer_size, uint32_t txring_count,
                        bool contiguous) {
    if (txring->buffer_vmo || txring->txring_vmo) {
        return ERR_ALREADY_EXISTS;
    }

    mx_handle_t buffer_vmo = MX_HANDLE_INVALID;
    mx_handle_t txring_vmo = MX_HANDLE_INVALID;
    uint8_t* buffer = NULL;
    mx_txring_entry_t* ring = NULL;
    mx_status_t status;

    if (contiguous) {
        status = mx_vmo_create_contiguous(get_root_resource(), buffer_size, &buffer_vmo);
    } else {
        status = mx_vmo_create(buffer_size, 0, &buffer_vmo);
    }
    if (status < 0) {
        printf("failed to create buffer VMO: %d\n", status);
        goto fail;
    }
    status = mx_vmo_create(txring_count * sizeof(mx_txring_entry_t), 0, &txring_vmo);
    if (status < 0) {
        printf("failed to create txring VMO: %d\n", status);
        goto fail;
    }

    status = mx_process_map_vm(mx_process_self(), buffer_vmo, 0, buffer_size, (uintptr_t *)&buffer,
                               MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
    if (status < 0) {
        printf("failed to map buffer VMO: %d\n", status);
        goto fail;
    }
    status = mx_process_map_vm(mx_process_self(), txring_vmo, 0, sizeof(*ring) * txring_count,
                               (uintptr_t *)&ring, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
    if (status < 0) {
        printf("failed to txring VMO: %d\n", status);
        goto fail;
    }

    txring->buffer_vmo = buffer_vmo;
    txring->txring_vmo = txring_vmo;
    txring->buffer_size = buffer_size;
    txring->txring_count = txring_count;
    txring->buffer = buffer;
    txring->ring = ring;

    return NO_ERROR;

fail:
    txring_release(txring);
    return status;
}

void txring_release(txring_t* txring) {
    if (txring->buffer) {
        mx_process_unmap_vm(txring->buffer_vmo, (uintptr_t)txring->buffer, txring->buffer_size);
    }
    if (txring->ring) {
        mx_process_unmap_vm(txring->txring_vmo, (uintptr_t)txring->ring,
                            sizeof(mx_txring_entry_t) * txring->txring_count);
    }
    mx_handle_close(txring->buffer_vmo);
    mx_handle_close(txring->txring_vmo);
    memset(txring, 0, sizeof(*txring));
}
