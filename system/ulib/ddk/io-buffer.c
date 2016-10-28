// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/io-buffer.h>
#include <ddk/driver.h>
#include <magenta/syscalls.h>
#include <limits.h>
#include <stdio.h>

mx_status_t io_buffer_init(io_buffer_t* buffer, size_t size) {
    if (size == 0) return ERR_INVALID_ARGS;
    
    mx_handle_t vmo_handle;
    mx_status_t status = mx_alloc_contiguous_memory(get_root_resource(), size, &vmo_handle);
    if (status != NO_ERROR) {
        printf("io_buffer: mx_alloc_contiguous_memory failed %d\n", vmo_handle);
        return status;
    }

    mx_vaddr_t virt;
    status = mx_process_map_vm(mx_process_self(), vmo_handle, 0, size, &virt,
                                           MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
    if (status != NO_ERROR) {
        printf("io_buffer: mx_process_map_vm failed %d size: %zu\n", status, size);
        mx_handle_close(vmo_handle);
        return status;
    }

    mx_paddr_t phys;
    status = mx_vmo_op_range(vmo_handle, MX_VMO_OP_LOOKUP, 0, PAGE_SIZE, &phys, sizeof(phys));
    if (status != NO_ERROR) {
        printf("io_buffer: mx_vmo_op_range failed %d size: %zu\n", status, size);
        mx_handle_close(vmo_handle);
        return status;
    }

    buffer->vmo_handle = vmo_handle;
    buffer->size = size;
    buffer->buffer = (void *)virt;
    buffer->phys = phys;
    return NO_ERROR;
}

void io_buffer_free(io_buffer_t* buffer) {
    mx_handle_close(buffer->vmo_handle);
}
