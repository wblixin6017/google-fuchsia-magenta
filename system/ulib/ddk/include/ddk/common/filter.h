// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Move me

#include <ddk/device.h>
#include <ddk/driver.h>

// Forward declarations
typedef struct filter filter_t;

typedef struct filter_worker filter_worker_t;

typedef void (*filter_worker_f)(iotxn_t* txn);

// Types

typedef struct filter_ops_t {
    mx_status_t (*release)(filter_t* filter);
    mx_status_t (*validate_iotxn)(iotxn_t* cloned);
    mx_off_t (*get_size)(filter_t* filter, mx_off_t parent_size);
    ssize_t (*ioctl)(filter_t* filter, uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max);
} filter_ops_t;

// General utility subroutines

filter_t* filter_get(mx_device_t* dev);

mx_device_t* filter_dev(filter_t* filter);

filter_t* filter_from_cloned(iotxn_t *cloned);

// Protocol support subroutines

void filter_assign(iotxn_t* txn, filter_worker_t* worker, bool skip_validation);

void filter_continue(iotxn_t* cloned);

void filter_complete(iotxn_t* cloned);

// Bind subroutines

filter_t* filter_init(mx_driver_t* drv,
                      const char* name,
                      uint32_t protocol_id,
                      const filter_ops_t* ops);

filter_worker_t* filter_add_worker(filter_t* filter, filter_worker_f func, size_t num, bool is_default);

mx_status_t filter_add(filter_t* filter, mx_device_t* parent);
