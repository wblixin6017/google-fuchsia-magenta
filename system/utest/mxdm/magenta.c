// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides dummy implementations of a few syscalls that get invoked,
// in order to prevent the test from doing "real" work.

#include <stdint.h>
#include <stdlib.h>

#include <magenta/fuchsia-types.h>
#include <magenta/syscalls-types.h>
#include <magenta/types.h>

#define USER_PTR(ptr) ptr

mx_status_t mx_futex_wait(USER_PTR(volatile mx_futex_t) value_ptr,
                          mx_futex_t current_value, mx_time_t timeout) {
  return NO_ERROR;
}

mx_status_t mx_futex_wake(USER_PTR(volatile mx_futex_t) value_ptr,
                          uint32_t count) {
  return NO_ERROR;
}

mx_status_t mx_alloc_device_memory(mx_handle_t handle, uint32_t len,
                                   mx_paddr_t *out_paddr, void **out_vaddr) {
  void *buf = calloc(len, 1);
  if (!buf) {
    return ERR_NO_MEMORY;
  }
  *out_paddr = (mx_paddr_t)buf;
  *out_vaddr = buf;
  return NO_ERROR;
}
