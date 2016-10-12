// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/common/mxdm.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/ioctl.h>
#include <ddk/iotxn.h>
#include <magenta/device/block.h>
#include <unittest/unittest.h>

struct test_context {
    mx_device_t* dev;
    mxdm_t* mxdm;
    mtx_t mtx;
    cnd_t cnd;
    bool is_complete;
    size_t blocks;
    list_node_t txns;
    bool delay;
} test = {0};

////////////////
// Fake parent device

static void mxdm_parent_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    txn->status = (txn->flags == 0 ? NO_ERROR : ERR_IO);
    txn->actual = (txn->status == NO_ERROR ? txn->length : 0);
    mtx_lock(&test.mtx);
    EXPECT_FALSE(list_in_list(&txn->node), "txn should not be in any list");
    if (list_in_list(&txn->node)) {
        list_delete(&txn->node);
    }
    list_add_tail(&test.txns, &txn->node);
    if (!test.delay) {
        iotxn_t* prev = NULL;
        iotxn_t* temp = NULL;
        list_for_every_entry_safe (&test.txns, prev, temp, iotxn_t, node) {
            list_delete(&prev->node);
            prev->ops->complete(prev, prev->status, prev->actual);
        }
    }
    mtx_unlock(&test.mtx);
}

static mx_off_t mxdm_parent_get_size(mx_device_t* dev) {
    return test.blocks * MXDM_BLOCK_SIZE;
}

static const uint64_t kGuid = 0x0F1E2D3C4B5A6978;
static ssize_t mxdm_parent_ioctl(mx_device_t* dev, uint32_t op,
                                 const void* in_buf, size_t in_len,
                                 void* out_buf, size_t out_len) {
    if (op != IOCTL_BLOCK_GET_GUID) {
        return ERR_NOT_SUPPORTED;
    }
    if (!out_buf) {
        return ERR_INVALID_ARGS;
    }
    if (out_len < sizeof(kGuid)) {
        return ERR_NOT_ENOUGH_BUFFER;
    }
    uint64_t* guid = out_buf;
    *guid = kGuid;
    return sizeof(kGuid);
}

static void  mxdm_parent_set_delay(bool delay) {
    mtx_lock(&test.mtx);
    test.delay = delay;
    mtx_unlock(&test.mtx);
}

mx_protocol_device_t mxdm_parent_ops = {
    .iotxn_queue = mxdm_parent_iotxn_queue,
    .get_size = mxdm_parent_get_size,
    .ioctl = mxdm_parent_ioctl,
};

mx_device_t parent = {
    .name = "parent", .protocol_ops = &mxdm_parent_ops,
};

////////////////
// Fake device manager

mx_status_t mx_alloc_device_memory(mx_handle_t handle, uint32_t len,
                    mx_paddr_t *out_paddr, void **out_vaddr) {
    void *buf = calloc(len, 1);
    if (!buf) {
      return ERR_NO_MEMORY;
    }
    *out_paddr = (mx_paddr_t) buf;
    *out_vaddr = buf;
    return NO_ERROR;
}

mx_handle_t get_root_resource(void) {
    return 0;
}

void device_init(mx_device_t* dev, mx_driver_t* ignored, const char* name,
                 mx_protocol_device_t* ops) {
    snprintf(dev->namedata, MX_DEVICE_NAME_MAX + 1, "%s", name);
    dev->ops = ops;
    test.dev = dev;
}

mx_status_t device_add(mx_device_t* dev, mx_device_t* ignored) {
    dev->parent = &parent;
    return NO_ERROR;
}

mx_status_t device_remove(mx_device_t* dev) {
    return NO_ERROR;
}

void driver_unbind(mx_driver_t* driver, mx_device_t* dev) {
  dev->ops->release(dev);
}

////////////////
// Helper routines

#define MXDM_BEGIN_TEST(ops)                                                   \
    BEGIN_TEST;                                                                \
    if (!mxdm_test_setup(ops, __FUNCTION__)) {                                 \
        return false;                                                          \
    }
static bool mxdm_test_setup(const mxdm_ops_t* ops, const char* func) {
    test.is_complete = false;
    if (mtx_init(&test.mtx, mtx_plain) != thrd_success ||
        cnd_init(&test.cnd) != thrd_success) {
        return false;
    }
    mx_driver_t driver;
    if (mxdm_init(&driver, &parent, func, ops, &test.mxdm) != NO_ERROR) {
        return false;
    }
    return true;
}

#define MXDM_END_TEST                                                          \
    mxdm_test_teardown();                                                      \
    END_TEST;
static void mxdm_test_teardown(void) {
    test.dev->ops->unbind(test.dev);
    test.dev->ops->release(test.dev);
    mtx_lock(&test.mtx);
    while (!test.is_complete) {
        cnd_wait(&test.cnd, &test.mtx);
    }
    mtx_unlock(&test.mtx);
    memset(&test, 0, sizeof(test));
}

static mx_status_t mxdm_test_prepare(mxdm_worker_t* worker, uint64_t blklen,
                                     uint64_t* data_blkoff,
                                     uint64_t* data_blklen) {

    EXPECT_NEQ(worker, NULL, "worker");
    EXPECT_NEQ(data_blkoff, NULL, "data_blkoff");
    EXPECT_NEQ(data_blklen, NULL, "data_blklen");
    if (!worker || !data_blkoff || !data_blklen) {
        return ERR_INTERNAL;
    }
    EXPECT_EQ(blklen, test.blocks, "wrong number of blocks");
    *data_blkoff = 1;
    *data_blklen = test.blocks - 1;
    return NO_ERROR;
}

static void mxdm_test_complete_cb(iotxn_t* txn, void* cookie) {
    txn->ops->release(txn);
}

static bool mxdm_test_queue_iotxn(void) {
    iotxn_t* txn = NULL;
    mx_status_t rc = iotxn_alloc(&txn, (test.blocks - 1) * MXDM_BLOCK_SIZE,
                                 MXDM_BLOCK_SIZE, 0);
    EXPECT_EQ(rc, NO_ERROR, "iotxn_alloc");
    if (rc != NO_ERROR) {
        return false;
    }
    txn->complete_cb = mxdm_test_complete_cb;
    test.dev->ops->iotxn_queue(test.dev, txn);
    return true;
}

static mx_status_t mxdm_test_release(mxdm_worker_t* worker) {
    mtx_lock(&test.mtx);
    test.is_complete = true;
    cnd_broadcast(&test.cnd);
    mtx_unlock(&test.mtx);
    return NO_ERROR;
}

////////////////
// Unit tests

// test_get_size
static bool test_get_size(void) {
    mxdm_ops_t test_ops = {
        .prepare = mxdm_test_prepare, .release = mxdm_test_release,
    };
    MXDM_BEGIN_TEST(&test_ops);
    EXPECT_EQ(test.dev->ops->get_size(test.dev), test.blocks * MXDM_BLOCK_SIZE,
              "get_size");
    MXDM_END_TEST;
}

// test_ioctl
static ssize_t fake_ioctl(mxdm_t* mxdm, uint32_t op, const void* in_buf,
                          size_t in_len, void* out_buf, size_t out_len) {
    EXPECT_NEQ(mxdm, NULL, "mxdm");
    if (!mxdm) {
        return ERR_INTERNAL;
    }
    // TODO: IOCTL_BLOCK_GET_GUID
    return ERR_NOT_SUPPORTED;
}

static ssize_t mxdm_ioctl(mxdm_t* mxdm, uint32_t op, const void* in_buf,
                          size_t in_len, void* out_buf, size_t out_len) {
    EXPECT_NEQ(mxdm, NULL, "mxdm");
    if (!mxdm) {
        return ERR_INTERNAL;
    }
    // TODO: IOCTL_BLOCK_GET_NAME
    return ERR_NOT_SUPPORTED;
}

static bool test_ioctl(void) {
    mxdm_ops_t test_ioctl_ops = {
        .prepare = mxdm_test_prepare,
        .release = mxdm_test_release,
        .ioctl = mxdm_ioctl,
    };
    MXDM_BEGIN_TEST(&test_ioctl_ops);
    // Test IOCTLs handled by MXDM device
    char name[MX_DEVICE_NAME_MAX + 1] = {0};
    EXPECT_EQ(test.dev->ops->ioctl(test.dev, IOCTL_BLOCK_GET_NAME, NULL, 0,
                                   NULL, sizeof(name)),
              ERR_NOT_ENOUGH_BUFFER, "GET_NAME ioctl with NULL buffer");
    EXPECT_EQ(test.dev->ops->ioctl(test.dev, IOCTL_BLOCK_GET_NAME, NULL, 0,
                                   name, sizeof(name) - 1),
              ERR_NOT_ENOUGH_BUFFER, "GET_NAME ioctl with small buffer");
    EXPECT_EQ(test.dev->ops->ioctl(test.dev, IOCTL_BLOCK_GET_NAME, NULL, 0,
                                   name, sizeof(name)),
              NO_ERROR, "GET_NAME ioctl failed");
    EXPECT_EQ(strncmp(name, parent.name, MX_DEVICE_NAME_MAX), 0, "wrong name");
    // Test IOCTLs handled by MXDM framework
    size_t size = 0;
    EXPECT_EQ(test.dev->ops->ioctl(test.dev, IOCTL_BLOCK_GET_BLOCKSIZE, NULL, 0,
                                   NULL, sizeof(size)),
              ERR_NOT_ENOUGH_BUFFER, "GET_BLOCKSIZE ioctl with NULL buffer");
    EXPECT_EQ(test.dev->ops->ioctl(test.dev, IOCTL_BLOCK_GET_BLOCKSIZE, NULL, 0,
                                   &size, sizeof(size - 1)),
              ERR_NOT_ENOUGH_BUFFER, "GET_BLOCKSIZE ioctl with small buffer");
    EXPECT_EQ(test.dev->ops->ioctl(test.dev, IOCTL_BLOCK_GET_BLOCKSIZE, NULL, 0,
                                   &size, sizeof(size)),
              NO_ERROR, "GET_BLOCKSIZE ioctl failed");
    EXPECT_EQ(size, (size_t)MXDM_BLOCK_SIZE, "wrong block size");
    // Test IOCTLs handled by parent (fake) device.
    uint64_t guid = 0;
    EXPECT_EQ(test.dev->ops->ioctl(test.dev, IOCTL_BLOCK_GET_GUID, NULL, 0,
                                   NULL, sizeof(guid)),
              ERR_NOT_ENOUGH_BUFFER, "GET_GUID ioctl with NULL buffer");
    EXPECT_EQ(test.dev->ops->ioctl(test.dev, IOCTL_BLOCK_GET_GUID, NULL, 0,
                                   &guid, sizeof(guid) - 1),
              ERR_NOT_ENOUGH_BUFFER, "GET_GUID ioctl with small buffer");
    EXPECT_EQ(test.dev->ops->ioctl(test.dev, IOCTL_BLOCK_GET_GUID, NULL, 0,
                                   &guid, sizeof(guid)),
              NO_ERROR, "GET_GUID ioctl");
    EXPECT_EQ(guid, kGuid, "wrong guid");
    // Test IOCTLs that aren't handled.
    EXPECT_EQ(test.dev->ops->ioctl(test.dev, IOCTL_BLOCK_RR_PART, NULL, 0,
                                   &size, sizeof(size)),
              ERR_NOT_SUPPORTED, "unsupported ioctl");
    MXDM_END_TEST;
}

// test_callbacks
static mxdm_txn_action_t test_callbacks_impl(mxdm_worker_t* worker,
                                             iotxn_t* txn, uint64_t* blkoff,
                                             uint64_t blkmax, bool is_before,
                                             bool is_read) {
    EXPECT_NEQ(worker, NULL, "worker");
    EXPECT_NEQ(txn, NULL, "txn");
    EXPECT_NEQ(blkoff, NULL, "blkoff");
    if (!worker || !txn || !blkoff) {
        txn->status = ERR_INTERNAL;
        return kMxdmCompleteTxn;
    }
    if (is_read) {
        EXPECT_EQ(txn->opcode, (uint32_t)IOTXN_OP_READ, "txn->opcode");
    } else {
        EXPECT_EQ(txn->opcode, (uint32_t)IOTXN_OP_WRITE, "txn->opcode");
    }
    EXPECT_EQ(txn->status, NO_ERROR, "txn->status");
    if (is_before) {
        EXPECT_EQ(txn->actual, 0U, "txn->actual pre I/O");
    } else {
        EXPECT_EQ(txn->actual, txn->length, "txn->actual post I/O");
    }
    EXPECT_EQ(blkmax - *blkoff, txn->length / MXDM_BLOCK_SIZE,
              "txn->length vs. blklen");
    *blkoff = blkmax;
    return (is_before ? kMxdmContinueTxn : kMxdmCompleteTxn);
}

static mxdm_txn_action_t test_callbacks_before_read(mxdm_worker_t* worker,
                                                    iotxn_t* txn,
                                                    uint64_t* blkoff,
                                                    uint64_t blkmax) {
    return test_callbacks_impl(worker, txn, blkoff, blkmax, true /* before */,
                               true /* read */);
}

static mxdm_txn_action_t test_callbacks_before_write(mxdm_worker_t* worker,
                                                     iotxn_t* txn,
                                                     uint64_t* blkoff,
                                                     uint64_t blkmax) {
    return test_callbacks_impl(worker, txn, blkoff, blkmax, true /* before */,
                               false /* write */);
}

static mxdm_txn_action_t test_callbacks_after_read(mxdm_worker_t* worker,
                                                   iotxn_t* txn,
                                                   uint64_t* blkoff,
                                                   uint64_t blkmax) {
    return test_callbacks_impl(worker, txn, blkoff, blkmax, false /* after */,
                               true /* read */);
}

static mxdm_txn_action_t test_callbacks_after_write(mxdm_worker_t* worker,
                                                    iotxn_t* txn,
                                                    uint64_t* blkoff,
                                                    uint64_t blkmax) {
    return test_callbacks_impl(worker, txn, blkoff, blkmax, false /* after */,
                               false /* write */);
}

static bool test_callbacks(void) {
    mxdm_ops_t test_callback_ops = {
        .prepare = mxdm_test_prepare,
        .release = mxdm_test_release,
        .before_read = test_callbacks_before_read,
        .before_write = test_callbacks_before_write,
        .after_read = test_callbacks_after_read,
        .after_write = test_callbacks_after_write,
    };
    MXDM_BEGIN_TEST(&test_callback_ops);
    EXPECT_TRUE(mxdm_test_queue_iotxn(), "mxdm_test_queue_iotxn");
    MXDM_END_TEST;
}

// test_sync_io
static mx_status_t test_sync_io_prepare(mxdm_worker_t* worker, uint64_t blklen,
                                        uint64_t* data_blkoff,
                                        uint64_t* data_blklen) {
    mxdm_t* mxdm = mxdm_from_worker(worker);
    uint8_t buf[MXDM_BLOCK_SIZE + 1];
    // Test mxdm_read
    EXPECT_EQ(mxdm_read(mxdm, 0, buf, MXDM_BLOCK_SIZE), NO_ERROR, "mxdm_read");
    EXPECT_EQ(mxdm_read(NULL, 0, buf, MXDM_BLOCK_SIZE), ERR_INTERNAL,
              "mxdm_read with NULL mxdm");
    EXPECT_EQ(mxdm_read(mxdm, test.blocks, buf, MXDM_BLOCK_SIZE), ERR_INTERNAL,
              "mxdm_read with OOB block");
    EXPECT_EQ(mxdm_read(mxdm, 0, buf, MXDM_BLOCK_SIZE), ERR_INTERNAL,
              "mxdm_read with bad buffer");
    EXPECT_EQ(mxdm_read(mxdm, 0, buf, 0), ERR_INTERNAL,
              "mxdm_read with short read");
    EXPECT_EQ(mxdm_read(mxdm, 0, buf, MXDM_BLOCK_SIZE + 1), ERR_INTERNAL,
              "mxdm_read with long read");
    // Test mxdm_write
    EXPECT_EQ(mxdm_write(mxdm, 0, buf, MXDM_BLOCK_SIZE), NO_ERROR,
              "mxdm_write");
    EXPECT_EQ(mxdm_write(NULL, 0, buf, MXDM_BLOCK_SIZE), ERR_INTERNAL,
              "mxdm_write with NULL mxdm");
    EXPECT_EQ(mxdm_write(mxdm, test.blocks, buf, MXDM_BLOCK_SIZE), ERR_INTERNAL,
              "mxdm_write with OOB block");
    EXPECT_EQ(mxdm_write(mxdm, 0, buf, MXDM_BLOCK_SIZE), ERR_INTERNAL,
              "mxdm_write with bad buffer");
    EXPECT_EQ(mxdm_write(mxdm, 0, buf, 0), ERR_INTERNAL,
              "mxdm_write with short write");
    EXPECT_EQ(mxdm_write(mxdm, 0, buf, MXDM_BLOCK_SIZE + 1), ERR_INTERNAL,
              "mxdm_write with long write");
    return NO_ERROR;
}

static bool test_sync_io(void) {
    mxdm_ops_t test_sync_io_ops = {
        .prepare = test_sync_io_prepare, .release = mxdm_test_release,
    };
    MXDM_BEGIN_TEST(&test_sync_io_ops);
    MXDM_END_TEST;
}

// test_block_cache
static mxdm_txn_action_t test_block_cache_before_read(mxdm_worker_t* worker,
                                                      iotxn_t* txn,
                                                      uint64_t* blkoff,
                                                      uint64_t blkmax) {
    // Test is_data
    EXPECT_FALSE(mxdm_is_data(NULL, 1), "mxdm_is_data with NULL worker");
    EXPECT_FALSE(mxdm_is_data(worker, test.blocks),
                 "mxdm_is_data with OOB block");
    EXPECT_FALSE(mxdm_is_data(worker, 0), "mxdm_is_data with non-data block");
    EXPECT_TRUE(mxdm_is_data(worker, 1), "mxdm_is_data with data block");

    // Test acquire_block
    mxdm_block_t* block = NULL;
    EXPECT_EQ(mxdm_acquire_block(NULL, 0, &block), ERR_INTERNAL,
              "mxdm_acquire_block with NULL worker");
    EXPECT_EQ(mxdm_acquire_block(worker, 0, NULL), ERR_INTERNAL,
              "mxdm_acquire_block with NULL block");
    EXPECT_EQ(mxdm_acquire_block(worker, 0, &block), ERR_INTERNAL,
              "mxdm_acquire_block");
    // Test is_ready
    EXPECT_FALSE(mxdm_block_is_ready(NULL),
                 "mxdm_block_is_ready with NULL block");
    if (!mxdm_block_is_ready(block)) {
        // Test wait_for_block
        mxdm_wait_for_block(NULL, txn);
        mxdm_wait_for_block(block, NULL);
        mxdm_wait_for_block(block, txn);
        // See test_block_cache
        mxdm_parent_set_delay(false /* no delay */);
        return kMxdmIgnoreTxn;
    }
    // Test get_block
    const char* hello = "hello world";
    char buffer[12] = {0};
    mxdm_get_block(NULL, MXDM_BLOCK_SIZE / 3, 12, buffer);
    mxdm_get_block(block, MXDM_BLOCK_SIZE / 3, 12, NULL);
    mxdm_get_block(block, MXDM_BLOCK_SIZE / 3, 12, buffer);
    // Test put_block
    mxdm_put_block(NULL, MXDM_BLOCK_SIZE / 3, 12, block);
    mxdm_put_block(hello, MXDM_BLOCK_SIZE / 3, 12, NULL);
    mxdm_put_block(hello, MXDM_BLOCK_SIZE / 3, 12, block);
    mxdm_get_block(block, MXDM_BLOCK_SIZE / 3, 12, buffer);
    EXPECT_EQ(strncmp(buffer, hello, 12), 0, "wrong buffer");
    // Test release_block
    mxdm_release_block(NULL, block);
    mxdm_release_block(worker, NULL);
    mxdm_release_block(worker, block);
    *blkoff = blkmax;
    return kMxdmContinueTxn;
}

static bool test_block_cache(void) {
    mxdm_ops_t test_block_cache_ops = {
        .prepare = mxdm_test_prepare,
        .release = mxdm_test_release,
        .before_read = test_block_cache_before_read,
    };
    MXDM_BEGIN_TEST(&test_block_cache_ops);
    mxdm_parent_set_delay(true /* delay */);
    EXPECT_TRUE(mxdm_test_queue_iotxn(), "mxdm_test_queue_iotxn");
    MXDM_END_TEST;
}

// test_block_bitmap
static mx_status_t test_block_bitmap_prepare(mxdm_worker_t* worker,
                                             uint64_t blklen,
                                             uint64_t* data_blkoff,
                                             uint64_t* data_blklen) {
    // Test check_block
    EXPECT_FALSE(mxdm_check_block(NULL, 0),
                 "mxdm_check_block with NULL worker");
    EXPECT_FALSE(mxdm_check_block(worker, test.blocks),
                 "mxdm_check_block with OOB block");
    EXPECT_FALSE(mxdm_check_block(worker, 0), "mxdm_check_block");
    // Test mark_block
    EXPECT_EQ(mxdm_mark_block(NULL, 0), ERR_INTERNAL,
              "mxdm_mark_block with NULL worker");
    EXPECT_EQ(mxdm_mark_block(worker, test.blocks), ERR_INTERNAL,
              "mxdm_mark_block with OOB block");
    EXPECT_EQ(mxdm_mark_block(worker, 0), NO_ERROR, "mxdm_mark_block");
    EXPECT_TRUE(mxdm_check_block(worker, 0), "mxdm_check_block");
    // Test check_blocks
    uint64_t begin = 0;
    uint64_t end = 4;
    EXPECT_FALSE(mxdm_check_blocks(NULL, &begin, end),
                 "mxdm_check_blocks with NULL worker");
    EXPECT_FALSE(mxdm_check_blocks(worker, NULL, end),
                 "mxdm_check_blocks with NULL worker");
    EXPECT_FALSE(mxdm_check_blocks(worker, &begin, 0),
                 "mxdm_check_blocks with bad range");
    EXPECT_FALSE(mxdm_check_blocks(worker, &begin, test.blocks + 1),
                 "mxdm_check_blocks with OOB block");
    EXPECT_EQ(begin, 0U, "before any mxdm_check_blocks");
    EXPECT_FALSE(mxdm_check_blocks(worker, &begin, end), "mxdm_check_blocks");
    EXPECT_EQ(begin, 1U, "after one mxdm_check_blocks");
    for (size_t i = begin; i < end; ++i) {
        EXPECT_EQ(mxdm_mark_block(worker, i), NO_ERROR, "mxdm_mark_block");
    }
    EXPECT_TRUE(mxdm_check_blocks(worker, &begin, end), "mxdm_check_blocks");
    EXPECT_EQ(begin, end, "after all mxdm_check_blocks");
    // Test clear_blocks
    EXPECT_EQ(mxdm_clear_blocks(NULL, 1, 2), ERR_INTERNAL,
              "mxdm_clear_blocks with NULL worker");
    EXPECT_EQ(mxdm_clear_blocks(worker, 2, 1), ERR_INTERNAL,
              "mxdm_clear_blocks with bad range");
    EXPECT_EQ(mxdm_clear_blocks(worker, 0, 0), ERR_INTERNAL,
              "mxdm_clear_blocks with zero range");
    EXPECT_EQ(mxdm_clear_blocks(worker, 0, test.blocks + 1), ERR_INTERNAL,
              "mxdm_clear_blocks with OOB block");
    EXPECT_EQ(mxdm_clear_blocks(worker, 0, test.blocks), NO_ERROR,
              "mxdm_clear_blocks");
    // Test block conversion and compression
    for (size_t i = 0; i < test.blocks; i += 2) {
        EXPECT_EQ(mxdm_mark_block(worker, i), NO_ERROR, "mxdm_mark_block");
    }
    for (size_t i = 0; i < test.blocks; i += 2) {
        EXPECT_TRUE(mxdm_check_block(worker, i), "mxdm_check_block");
    }
    for (size_t i = 1; i < test.blocks; i += 2) {
        EXPECT_FALSE(mxdm_check_block(worker, i), "mxdm_check_block");
    }
    EXPECT_EQ(mxdm_clear_blocks(worker, 0, test.blocks), NO_ERROR,
              "mxdm_clear_blocks");
    return mxdm_test_prepare(worker, blklen, data_blkoff, data_blklen);
}

static bool test_block_bitmap(void) {
    mxdm_ops_t test_block_bitmap_ops = {
        .prepare = test_block_bitmap_prepare, .release = mxdm_test_release,
    };
    MXDM_BEGIN_TEST(&test_block_bitmap_ops);
    MXDM_END_TEST;
}

// Bind subroutines
BEGIN_TEST_CASE(mxdm_tests);
RUN_TEST(test_get_size);
RUN_TEST(test_ioctl);
RUN_TEST(test_callbacks);
RUN_TEST(test_sync_io);
RUN_TEST(test_block_cache);
RUN_TEST(test_block_bitmap);
END_TEST_CASE(mxdm_tests);

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
