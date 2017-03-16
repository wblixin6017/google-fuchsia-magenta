// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <ddk/io-buffer.h>
#include <ddk/driver.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <limits.h>
#include <stdio.h>

#define ROUNDUP(a, b) (((a)+ ((b)-1)) & ~((b)-1))

static mx_status_t io_buffer_init_common(io_buffer_t* buffer, mx_handle_t vmo_handle, size_t size,
                                         mx_off_t offset, uint32_t flags) {
    mx_vaddr_t virt;

    mx_status_t status = mx_vmar_map(mx_vmar_root_self(), 0, vmo_handle, 0, size, flags, &virt);
    if (status != NO_ERROR) {
        printf("io_buffer: mx_vmar_map failed %d size: %zu\n", status, size);
        mx_handle_close(vmo_handle);
        return status;
    }

    size_t paddr_array_size = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE * sizeof(mx_paddr_t);
    mx_paddr_t *phys = calloc(1, paddr_array_size);

    status = mx_vmo_op_range(vmo_handle, MX_VMO_OP_LOOKUP, 0, size, phys, paddr_array_size);
    if (status != NO_ERROR) {
        printf("io_buffer: mx_vmo_op_range failed %d size: %zu\n", status, size);
        mx_vmar_unmap(mx_vmar_root_self(), virt, size);
        mx_handle_close(vmo_handle);
        free(phys);
        return status;
    }

    // validate that the physical addresses are contiguous
    mx_paddr_t last = phys[0];
    for (size_t i = 1; i < paddr_array_size / sizeof(mx_paddr_t); i++) {
        if (phys[i] != last + PAGE_SIZE) {
            printf("io_buffer: vmo's physical pages aren't contiguous!\n");
            mx_vmar_unmap(mx_vmar_root_self(), virt, size);
            mx_handle_close(vmo_handle);
            free(phys);
            return ERR_INVALID_ARGS;
        }
        last = phys[i];
    }

    buffer->vmo_handle = vmo_handle;
    buffer->size = size;
    buffer->offset = offset;
    buffer->virt = (void *)virt;
    buffer->phys = phys[0];
    free(phys);
    return NO_ERROR;
}

mx_status_t io_buffer_init_aligned(io_buffer_t* buffer, size_t size, uint32_t alignment_log2, uint32_t flags) {
    if (size == 0) {
        return ERR_INVALID_ARGS;
    }
    if (flags != IO_BUFFER_RO && flags != IO_BUFFER_RW) {
        return ERR_INVALID_ARGS;
    }

    mx_handle_t vmo_handle;
    mx_status_t status = mx_vmo_create_contiguous(get_root_resource(), size, alignment_log2, &vmo_handle);
    if (status != NO_ERROR) {
        printf("io_buffer: mx_vmo_create failed %d\n", vmo_handle);
        return status;
    }

    return io_buffer_init_common(buffer, vmo_handle, size, 0, flags);
}

mx_status_t io_buffer_init(io_buffer_t* buffer, size_t size, uint32_t flags) {
    // A zero alignment gets interpreted as PAGE_SIZE_SHIFT.
    return io_buffer_init_aligned(buffer, size, 0, flags);
}

mx_status_t io_buffer_init_vmo(io_buffer_t* buffer, mx_handle_t vmo_handle, mx_off_t offset,
                               uint32_t flags) {
    uint64_t size;

    if (flags != IO_BUFFER_RO && flags != IO_BUFFER_RW) {
        return ERR_INVALID_ARGS;
    }

    mx_status_t status = mx_handle_duplicate(vmo_handle, MX_RIGHT_SAME_RIGHTS, &vmo_handle);
    if (status != NO_ERROR) return status;

    status = mx_vmo_get_size(vmo_handle, &size);
    if (status != NO_ERROR) return status;

    return io_buffer_init_common(buffer, vmo_handle, size, offset, flags);
}

// copies an io_buffer. clone gets duplicate of the source's vmo_handle
mx_status_t io_buffer_clone(io_buffer_t* src, io_buffer_t* dest) {
    mx_status_t status = mx_handle_duplicate(src->vmo_handle, MX_RIGHT_SAME_RIGHTS,
                                             &dest->vmo_handle);
    if (status < 0) return status;
    dest->size = src->size;
    dest->offset = src->offset;
    dest->virt = src->virt;
    dest->phys = src->phys;
    return NO_ERROR;
}

void io_buffer_release(io_buffer_t* buffer) {
    if (buffer->vmo_handle) {
        mx_handle_close(buffer->vmo_handle);
        buffer->vmo_handle = MX_HANDLE_INVALID;
    }
}

mx_status_t io_buffer_cache_op(io_buffer_t* buffer, const uint32_t op,
                               const mx_off_t offset, const size_t size) {
    return mx_vmo_op_range(buffer->vmo_handle, op, offset, size, NULL, 0);
}
