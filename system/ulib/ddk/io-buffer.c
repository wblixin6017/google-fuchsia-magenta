// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/io-buffer.h>
#include <ddk/driver.h>
#include <magenta/syscalls.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static mx_status_t io_buffer_init_common(io_buffer_t* buffer, mx_handle_t vmo_handle, size_t size,
                                         mx_off_t offset, uint32_t flags) {
    mx_vaddr_t virt;

    int pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t phys_addrs_size = pages * sizeof(mx_paddr_t);
    mx_paddr_t* phys_addrs = malloc(phys_addrs_size);
    if (!phys_addrs) return ERR_NO_MEMORY;

    uint32_t vm_flags = MX_VM_FLAG_DMA;
    // Temporary hack to ensure our mapping does not conflict with DSO loading
    vm_flags |= MX_VM_FLAG_ALLOC_BASE;

    if (flags & IO_BUFFER_RO) vm_flags |= MX_VM_FLAG_PERM_READ;
    if (flags & IO_BUFFER_WO) vm_flags |= MX_VM_FLAG_PERM_WRITE;

    mx_status_t status = mx_vmar_map(mx_vmar_root_self(), 0, vmo_handle, 0, size, vm_flags, &virt);
    if (status != NO_ERROR) {
        printf("io_buffer: mx_vmar_map failed %d size: %zu\n", status, size);
        mx_handle_close(vmo_handle);
        free(phys_addrs);
        return status;
    }

    status = mx_vmo_op_range(vmo_handle, MX_VMO_OP_LOOKUP, 0, size, phys_addrs, phys_addrs_size);
    if (status != NO_ERROR) {
        printf("io_buffer: mx_vmo_op_range failed %d size: %zu\n", status, size);
        mx_handle_close(vmo_handle);
        free(phys_addrs);
        return status;
    }

    buffer->vmo_handle = vmo_handle;
    buffer->size = size;
    buffer->offset = offset;
    buffer->virt = (void *)virt;
    buffer->phys_addrs = phys_addrs;
    return NO_ERROR;
}

mx_status_t io_buffer_init(io_buffer_t* buffer, size_t size, uint32_t flags) {
    if (size == 0) {
        return ERR_INVALID_ARGS;
    }
    if (flags == 0 || flags & ~(IO_BUFFER_RO | IO_BUFFER_WO | IO_BUFFER_CONTIG)) {
        return ERR_INVALID_ARGS;
    }

    mx_handle_t vmo_handle;
    if (flags & IO_BUFFER_CONTIG && size > PAGE_SIZE) {
        mx_status_t status = mx_vmo_create_contiguous(get_root_resource(), size, &vmo_handle);
        if (status != NO_ERROR) {
            printf("io_buffer: mx_vmo_create_contiguous failed %d\n", vmo_handle);
            return status;
        }
    } else {
        mx_status_t status = mx_vmo_create(size, 0, &vmo_handle);
        if (status != NO_ERROR) {
            printf("io_buffer: mx_vmo_create failed %d\n", status);
            return status;
        }
        status = mx_vmo_op_range(vmo_handle, MX_VMO_OP_COMMIT, 0, size, NULL, 0);
        if (status != NO_ERROR) {
            printf("io_buffer: mx_vmo_op_range(MX_VMO_OP_COMMIT) failed %d\n", status);
            mx_handle_close(vmo_handle);
            return status;
        }
    }

    return io_buffer_init_common(buffer, vmo_handle, size, 0, flags);
}

mx_status_t io_buffer_init_vmo(io_buffer_t* buffer, mx_handle_t vmo_handle, mx_off_t offset,
                               uint32_t flags) {
    uint64_t size;

    if (flags == 0 || flags & ~(IO_BUFFER_RO | IO_BUFFER_WO)) {
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

    int pages = (src->size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t phys_addrs_size = pages * sizeof(mx_paddr_t);
    mx_paddr_t* phys_addrs = malloc(phys_addrs_size);
    if (!phys_addrs) {
        mx_handle_close(dest->vmo_handle);
        return ERR_NO_MEMORY;
    }
    memcpy(phys_addrs, src->phys_addrs, phys_addrs_size);
    dest->phys_addrs = phys_addrs;
    dest->size = src->size;
    dest->offset = src->offset;
    dest->virt = src->virt;
    return NO_ERROR;
}

void io_buffer_release(io_buffer_t* buffer) {
    if (buffer->vmo_handle) {
        mx_handle_close(buffer->vmo_handle);
        buffer->vmo_handle = MX_HANDLE_INVALID;
    }
    free(buffer->phys_addrs);
    buffer->phys_addrs = NULL;
}

mx_paddr_t io_buffer_phys(io_buffer_t* buffer, mx_off_t offset) {
    offset += buffer->offset;
    if (offset > buffer->size) {
        printf("offset out of bounds in io_buffer_phys()\n");
        return 0;
    }
    return buffer->phys_addrs[offset / PAGE_SIZE] + offset % PAGE_SIZE;
}

mx_status_t io_buffer_cache_op(io_buffer_t* buffer, const uint32_t op,
                               const mx_off_t offset, const size_t size) {
    return mx_vmo_op_range(buffer->vmo_handle, op, offset, size, NULL, 0);
}