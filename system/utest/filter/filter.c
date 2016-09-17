// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/common/filter.h>

#include <string.h>
#include <stdlib.h>

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/ioctl.h>
#include <magenta/device/block.h>
#include <unittest/unittest.h>

#define DEV_NAME "test"
#define DEV_SIZE 0x1000

// Fake out devmgr

mx_handle_t get_root_resource(void) {
    return 0;
}

void device_init(mx_device_t* dev, mx_driver_t* drv,
                 const char* name, mx_protocol_device_t* ops) {
}

mx_status_t device_add(mx_device_t* dev, mx_device_t* parent) {
    dev->parent = parent;
    return NO_ERROR;
}

mx_status_t device_remove(mx_device_t* dev) {
    return NO_ERROR;
}

// Fake out a device

static void filter_test_dev_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    // Simulate I/O error
    if (txn->opcode != IOTXN_OP_READ) {
        txn->ops->complete(txn, ERR_NOT_SUPPORTED, 0);
        return;
    }
    // "Read" some data.
    uint8_t* data;
    txn->ops->mmap(txn, (void**)(&data));
    for (size_t i = 0; i < txn->length; ++i) {
        data[i] = (uint8_t)(txn->offset + i);
    }
    txn->ops->complete(txn, NO_ERROR, txn->length);
}

static mx_off_t filter_test_dev_get_size(mx_device_t* dev) {
    return DEV_SIZE;
}

static ssize_t filter_test_dev_ioctl(mx_device_t* dev, uint32_t op,
                                     const void* in_buf, size_t in_len,
                                     void* out_buf, size_t out_len) {
    switch (op) {
    case IOCTL_BLOCK_GET_SIZE: {
        mx_off_t* size = out_buf;
        if (out_len < sizeof(*size))
            return ERR_NOT_ENOUGH_BUFFER;
        *size = filter_test_dev_get_size(dev);
        return sizeof(*size);
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_protocol_device_t filter_test_dev_ops = {
    .iotxn_queue = filter_test_dev_iotxn_queue,
    .get_size = filter_test_dev_get_size,
    .ioctl = filter_test_dev_ioctl,
};

static mx_device_t filter_test_dev = {
    .ops = &filter_test_dev_ops,
};

// Fake out a filter driver

static mx_off_t filter_test_get_size(filter_t* filter, mx_off_t parent_size) {
    return parent_size / 2;
}

static mx_status_t filter_test_validate_iotxn(iotxn_t* cloned) {
    filter_t* filter = cloned->cookie;
    mx_device_t* dev = filter_dev(filter);
    mx_off_t size = dev->ops->get_size(dev);
    if (cloned->offset + cloned->length > size) {
        return ERR_INVALID_ARGS;
    }
    return NO_ERROR;
}

static ssize_t filter_test_ioctl(filter_t* filter, uint32_t op,
                                 const void* in_buf, size_t in_len,
                                 void* out_buf, size_t out_len) {
    switch (op) {
    case IOCTL_BLOCK_GET_NAME: {
        char* name = out_buf;
        const mx_device_t* dev = filter_dev(filter);
        strncpy(name, dev->name, out_len - 1);
        name[out_len - 1] = 0;
        return strlen(name) + 1;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static filter_ops_t filter_test_ops = {
    .get_size = filter_test_get_size,
    .validate_iotxn = filter_test_validate_iotxn,
    .ioctl = filter_test_ioctl,
};

//

static void filter_test_worker(iotxn_t* cloned) {
    if (cloned->status != NO_ERROR) {
        filter_complete(cloned);
        return;
    }
    uint8_t* data;
    cloned->ops->mmap(cloned, (void**)(&data));
    for (size_t i = 0; i < cloned->length; ++i) {
        data[i] ^= 0xCC;
    }
    filter_complete(cloned);
}

// Helper routines for testing

static void filter_test_free(filter_t* filter) {
    if (!filter) {
        return;
    }
    mx_device_t* dev = filter_dev(filter);
    dev->ops->unbind(dev);
    dev->ops->release(dev);
}

static mx_device_t* filter_test_init(filter_ops_t* ops, filter_worker_f func) {
    filter_t* filter = filter_init(NULL, DEV_NAME, MX_PROTOCOL_BLOCK, ops);
    if (!filter) {
        goto fail;
    }
    if (!filter_add_worker(filter, func, 1, true /* is_default */)) {
        goto fail;
    }
    filter_add(filter, &filter_test_dev);
    return filter_dev(filter);
fail:
    filter_test_free(filter);
    return NULL;
}

static mx_device_t* filter_test_default_init(void) {
    return filter_test_init(NULL, NULL);
}

static mx_device_t* filter_test_hooked_init(void) {
    return filter_test_init(&filter_test_ops, filter_test_worker);
}

static void filter_tests_complete_cb(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

static void filter_test_reset_iotxn(iotxn_t* txn, size_t length) {
    txn->opcode = IOTXN_OP_READ;
    txn->offset = 0;
    txn->length = length;
    txn->complete_cb = filter_tests_complete_cb;
    completion_t* completion = txn->cookie;
    completion_reset(completion);
}

// Unit tests!

bool test_init(void) {
    BEGIN_TEST;
    mx_device_t* dev = filter_test_default_init();
    EXPECT_NEQ(dev, NULL, "filter_test_init failed");
    // Check device.
    EXPECT_EQ(dev->name, DEV_NAME, "device name mismatch");
    EXPECT_EQ(dev->protocol_id, (uint32_t) MX_PROTOCOL_BLOCK, "protocol ID mismatch");
    EXPECT_EQ(dev->ops, &filter_test_dev_ops, "device protocol mismatch");
    // Check filter.
    filter_t* filter = filter_get(dev);
    EXPECT_NEQ(filter, NULL, "unable to find filter");
    EXPECT_EQ(filter_dev(filter), dev, "filter device mismatch");
    filter_test_free(filter);
    END_TEST;
}

bool test_ioctl_default(void) {
    BEGIN_TEST;
    mx_device_t* dev = filter_test_default_init();
    EXPECT_NEQ(dev, NULL, "filter_test_init failed");
    // Try unsupported ioctl
    EXPECT_EQ(dev->ops->ioctl(dev, IOCTL_BLOCK_GET_GUID, NULL, 0, NULL, 0), ERR_NOT_SUPPORTED, "ioctl should be unsupported");
    // Try parent device ioctl
    size_t len = sizeof(mx_off_t);
    mx_off_t size;
    EXPECT_EQ(dev->ops->ioctl(dev, IOCTL_BLOCK_GET_SIZE, NULL, 0, &size, len), (ssize_t) len, "parent device ioctl failed");
    EXPECT_EQ(size, (mx_off_t) DEV_SIZE, "parent device ioctl returned wrong value");
    // Try filter device ioctl
    len = strlen(DEV_NAME) + 1;
    char* name = calloc(len, sizeof(char));
    EXPECT_NEQ(name, NULL, "out of memory");
    EXPECT_EQ(dev->ops->ioctl(dev, IOCTL_BLOCK_GET_NAME, NULL, 0, name, len), ERR_NOT_SUPPORTED, "filter device ioctl should have failed");
    free(name);
    END_TEST;
}

bool test_ioctl_hooked(void) {
    BEGIN_TEST;
    mx_device_t* dev = filter_test_hooked_init();
    EXPECT_NEQ(dev, NULL, "filter_test_init failed");
    // Try unsupported ioctl
    size_t len = 0;
    EXPECT_EQ(dev->ops->ioctl(dev, IOCTL_BLOCK_GET_GUID, NULL, 0, NULL, len), ERR_NOT_SUPPORTED, "ioctl should be unsupported");
    // Try parent device ioctl
    len = sizeof(mx_off_t);
    mx_off_t size;
    EXPECT_EQ(dev->ops->ioctl(dev, IOCTL_BLOCK_GET_SIZE, NULL, 0, &size, len), (ssize_t) len, "parent device ioctl failed");
    EXPECT_EQ(size, (mx_off_t) DEV_SIZE, "parent device ioctl returned wrong value");
    // Try filter device ioctl
    len = strlen(DEV_NAME) + 1;
    char* name = calloc(len, sizeof(char));
    EXPECT_NEQ(name, NULL, "out of memory");
    EXPECT_EQ(dev->ops->ioctl(dev, IOCTL_BLOCK_GET_NAME, NULL, 0, name, len), (ssize_t) len, "filter device ioctl failed");
    EXPECT_EQ(strncmp(name, DEV_NAME, len), 0, "filter device ioctl returned wrong value");
    free(name);
    END_TEST;
}

bool test_get_size_default(void) {
    BEGIN_TEST;
    mx_device_t* dev = filter_test_default_init();
    EXPECT_NEQ(dev, NULL, "filter_test_init failed");
    // Check size.
    mx_off_t size = dev->ops->get_size(dev);
    EXPECT_EQ(size, (mx_off_t) DEV_SIZE, "size mismatch");
    END_TEST;
}

bool test_get_size_hooked(void) {
    BEGIN_TEST;
    mx_device_t* dev = filter_test_hooked_init();
    EXPECT_NEQ(dev, NULL, "filter_test_init failed");
    // Check size.
    mx_off_t size = dev->ops->get_size(dev);
    EXPECT_EQ(size, (mx_off_t) DEV_SIZE / 2, "size mismatch");
    END_TEST;
}

bool test_iotxn_queue_default(void) {
    BEGIN_TEST;
    mx_device_t* dev = filter_test_default_init();
    iotxn_t* txn = NULL;
    EXPECT_EQ(iotxn_alloc(&txn, 0, 0, 0), NO_ERROR, "iotxn_alloc failed");
    completion_t completion = COMPLETION_INIT;
    txn->cookie = &completion;
    // Bypass a preprocessing error
    filter_test_reset_iotxn(txn, DEV_SIZE * 2);
    dev->ops->iotxn_queue(dev, txn);
    completion_wait(&completion, MX_TIME_INFINITE);
    EXPECT_EQ(txn->status, NO_ERROR, "no error expected without preprocessing");
    // Cause an I/O error
    filter_test_reset_iotxn(txn, DEV_SIZE / 2);
    txn->opcode = IOTXN_OP_WRITE;
    dev->ops->iotxn_queue(dev, txn);
    completion_wait(&completion, MX_TIME_INFINITE);
    EXPECT_EQ(txn->status, ERR_NOT_SUPPORTED, "parent device did not return expected error");
    EXPECT_EQ(txn->actual, 0U, "no data should be read on error");
    // Check read data without post-processing.
    filter_test_reset_iotxn(txn, DEV_SIZE / 2);
    dev->ops->iotxn_queue(dev, txn);
    completion_wait(&completion, MX_TIME_INFINITE);
    EXPECT_EQ(txn->actual, txn->length, "short read");
    uint8_t* data;
    txn->ops->mmap(txn, (void**)(&data));
    for (size_t i = 0; i < txn->actual; ++i) {
        EXPECT_EQ(data[i], (uint8_t)(i), "incorrect data");
    }
    END_TEST;
}

bool test_iotxn_queue_hooked(void) {
    BEGIN_TEST;
    mx_device_t* dev = filter_test_hooked_init();
    iotxn_t* txn = NULL;
    EXPECT_EQ(iotxn_alloc(&txn, 0, 0, 0), NO_ERROR, "iotxn_alloc failed");
    completion_t completion = COMPLETION_INIT;
    txn->cookie = &completion;
    // Cause a preprocessing error
    filter_test_reset_iotxn(txn, DEV_SIZE * 2);
    dev->ops->iotxn_queue(dev, txn);
    completion_wait(&completion, MX_TIME_INFINITE);
    EXPECT_EQ(txn->status, ERR_INVALID_ARGS, "preprocessing did not return expected error");
    EXPECT_EQ(txn->actual, 0U, "no data should be read on error");
    // Cause an I/O error
    filter_test_reset_iotxn(txn, DEV_SIZE / 2);
    txn->opcode = IOTXN_OP_WRITE;
    dev->ops->iotxn_queue(dev, txn);
    completion_wait(&completion, MX_TIME_INFINITE);
    EXPECT_EQ(txn->status, ERR_NOT_SUPPORTED, "parent device did not return expected error");
    EXPECT_EQ(txn->actual, 0U, "no data should be read on error");
    // Check read data with post-processing.
    filter_test_reset_iotxn(txn, DEV_SIZE / 2);
    dev->ops->iotxn_queue(dev, txn);
    completion_wait(&completion, MX_TIME_INFINITE);
    EXPECT_EQ(txn->actual, txn->length, "short read");
    uint8_t* data;
    txn->ops->mmap(txn, (void**)(&data));
    for (size_t i = 0; i < txn->actual; ++i) {
        EXPECT_EQ(data[i], (uint8_t)(i ^ 0xCC), "incorrect data");
    }
    END_TEST;
}

// Bind subroutines
BEGIN_TEST_CASE(filter_tests);
RUN_TEST(test_init);
RUN_TEST(test_ioctl_default);
RUN_TEST(test_ioctl_hooked);
RUN_TEST(test_get_size_default);
RUN_TEST(test_get_size_hooked);
RUN_TEST(test_iotxn_queue_default);
RUN_TEST(test_iotxn_queue_hooked);
END_TEST_CASE(filter_tests);

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
