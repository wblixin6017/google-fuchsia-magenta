// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private.h"

mx_status_t _mx_handle_close(mx_handle_t handle) {
    mx_status_t s = VDSO_mx_handle_close_internal(handle);
    if (s == -10101) {
        __builtin_trap();
    }
    return s;
}

__typeof(mx_handle_close) mx_handle_close
    __attribute__((weak, alias("_mx_handle_close")));
