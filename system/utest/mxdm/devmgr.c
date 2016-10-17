// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides dummy implementations of some of devmgr's interface, in
// order to allow testing independent of devmgr.

#include "private.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <magenta/fuchsia-types.h>
#include <magenta/listnode.h>
#include <magenta/types.h>

mx_handle_t get_root_resource(void) { return 0; }

void device_init(mx_device_t *dev, mx_driver_t *drv, const char *name,
                 mx_protocol_device_t *ops) {
  memset(dev, 0, sizeof(mx_device_t));
  snprintf(dev->name, MX_DEVICE_NAME_MAX + 1, "%s", name);
  dev->ops = ops;
  list_initialize(&dev->children);
}

mx_status_t device_add(mx_device_t *dev, mx_device_t *parent) {
  mxdm_test_ctx_t *ctx = parent->ctx;
  ctx->device = dev;
  dev->parent = parent;
  ctx->bound = true;
  return NO_ERROR;
}

mx_status_t device_remove(mx_device_t *dev) { return NO_ERROR; }

void driver_unbind(mx_driver_t *drv, mx_device_t *dev) {
  // A call to driver unbind indicates the worker thread failed to init after
  // mxdm_init returned.  In this case, mxdm_test_setup is waiting for the
  // mxdm_test_prepare to signal it after being called by the worker.
  mxdm_test_ctx_t *ctx = dev->ctx;
  ctx->bound = false;
  mxdm_test_sync_wake();
}
