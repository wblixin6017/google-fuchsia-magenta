// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

#include <stddef.h>

__BEGIN_CDECLS;

typedef struct {
    mx_handle_t vmo_handle;
    size_t size;
    void* buffer;
    mx_paddr_t phys;
} io_buffer_t;

mx_status_t io_buffer_init(io_buffer_t* buffer, size_t size);
void io_buffer_free(io_buffer_t* buffer);

__END_CDECLS;
