// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mxdm.h"

#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/iotxn.h>
#include <magenta/fuchsia-types.h>

#define CRYPT_TAG_LEN 16

typedef struct crypt_metadata {
    uint64_t counter;
    uint8_t tag[CRYPT_TAG_LEN];
} crypt_metadata_t;

typedef struct crypt {
    uint64_t data_blkoff;
    uint64_t primary_metadata_blkoff;
    uint64_t secondary_metadata_blkoff;
} crypt_t;

//

static uint64_t crypt_get_offsets(crypt_t* crypt, uint64_t blkoff,
                                  uint64_t* blkoff1, uint64_t* blkoff2) {
    uint64_t md_off = (blkoff - crypt->data_blkoff) * sizeof(crypt_metadata_t);
    uint64_t md_blk = md_off / MXDM_BLOCK_SIZE;
    *blkoff1 = md_blk + crypt->primary_metadata_blkoff;
    *blkoff2 = md_blk + crypt->secondary_metadata_blkoff;
    return md_off;
}

static void crypt_aead_seal(crypt_metadata_t metadata, iotxn_t* txn) {
    // TODO
}

static bool crypt_aead_open(crypt_metadata_t metadata, iotxn_t* txn) {
    // TODO
    return true;
}

//

static ssize_t crypt_ioctl(mxdm_device_t* device, uint32_t op, const void* in_buf,
                           size_t in_len, void* out_buf, size_t out_len) {
    return 0;
}

static mx_status_t crypt_prepare(mxdm_worker_t* worker, uint64_t blklen,
                                 uint64_t* data_blkoff, uint64_t* data_blklen) {
    return NO_ERROR;
}

static mx_status_t crypt_release(mxdm_worker_t* worker) {
    return NO_ERROR;
}

static mxdm_txn_action_t crypt_before_write(mxdm_worker_t* worker, iotxn_t* txn,
                                            uint64_t* blkoff, uint64_t blkmax) {
    crypt_t* crypt = mxdm_worker_get_context(worker);
    uint64_t blkoff1, blkoff2;
    while (*blkoff < blkmax) {
        uint64_t md_off = crypt_get_offsets(crypt, *blkoff, &blkoff1, &blkoff2);
        // todo translate off to off1/2
        mxdm_block_t* block1 = NULL;
        mxdm_block_t* block2 = NULL;
        if (!mxdm_acquire_block(worker, blkoff1, &block1) ||
            !mxdm_acquire_block(worker, blkoff2, &block2)) {
            return kMxdmRequeueTxn;
        }
        if (!mxdm_block_is_ready(block1)) {
            mxdm_wait_for_block(block1, txn);
            return kMxdmIgnoreTxn;
        }
        if (!mxdm_block_is_ready(block2)) {
            mxdm_wait_for_block(block2, txn);
            return kMxdmIgnoreTxn;
        }
        crypt_metadata_t metadata;
        mxdm_get_block(block1, md_off, sizeof(metadata), &metadata);
        ++metadata.counter;
        crypt_aead_seal(metadata, txn);
        mxdm_put_block(&metadata, md_off, sizeof(metadata), block1);
        mxdm_put_block(&metadata, md_off, sizeof(metadata), block2);
        mxdm_release_block(worker, block1);
        mxdm_release_block(worker, block2);
        ++*blkoff;
    }
    return kMxdmContinueTxn;
}

static mxdm_txn_action_t crypt_after_read(mxdm_worker_t* worker, iotxn_t* txn,
                                          uint64_t* blkoff, uint64_t blkmax) {
    crypt_t* crypt = mxdm_worker_get_context(worker);
    uint64_t blkoff1, blkoff2;
    while (*blkoff < blkmax) {
        uint64_t md_off = crypt_get_offsets(crypt, *blkoff, &blkoff1, &blkoff2);
        mxdm_block_t* block1 = NULL;
        mxdm_block_t* block2 = NULL;
        if (!mxdm_acquire_block(worker, blkoff1, &block1) ||
            !mxdm_acquire_block(worker, blkoff2, &block2)) {
            return kMxdmRequeueTxn;
        }
        if (!mxdm_block_is_ready(block1)) {
            mxdm_wait_for_block(block1, txn);
            return kMxdmIgnoreTxn;
        }
        if (!mxdm_block_is_ready(block2)) {
            mxdm_wait_for_block(block2, txn);
            return kMxdmIgnoreTxn;
        }
        crypt_metadata_t metadata1;
        crypt_metadata_t metadata2;
        mxdm_get_block(block1, md_off, sizeof(metadata1), &metadata1);
        mxdm_get_block(block2, md_off, sizeof(metadata2), &metadata2);
        if (crypt_aead_open(metadata1, txn)) {
            if (memcmp(&metadata1, &metadata2, sizeof(metadata1)) != 0) {
                mxdm_put_block(&metadata1, md_off, sizeof(metadata1), block2);
            }
        } else if (crypt_aead_open(metadata2, txn)) {
            if (memcmp(&metadata1, &metadata2, sizeof(metadata2)) != 0) {
                mxdm_put_block(&metadata2, md_off, sizeof(metadata2), block1);
            }
        } else {
            txn->status = ERR_IO_DATA_INTEGRITY;
            break;
        }
        mxdm_release_block(worker, block1);
        mxdm_release_block(worker, block2);
        ++*blkoff;
    }
    return kMxdmCompleteTxn;
}

//

static mxdm_device_ops_t crypt_device_ops = {
    .ioctl = crypt_ioctl,
};

static mxdm_worker_ops_t crypt_worker_ops = {
    .prepare = crypt_prepare,
    .release = crypt_release,
    .before_write = crypt_before_write,
    .after_read = crypt_after_read,
};

static mx_status_t crypt_bind(mx_driver_t* drv, mx_device_t* parent) {
    return mxdm_init(drv, parent, "crypt", &crypt_device_ops, &crypt_worker_ops,
                     sizeof(crypt_t));
}

mx_driver_t _driver_crypt = {
    .ops =
        {
            .bind = crypt_bind,
        },
    .flags = DRV_FLAG_NO_AUTOBIND,
};

MAGENTA_DRIVER_BEGIN(_driver_crypt, "mxdm-crypt", "magenta", "0.1", 1)
BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_BLOCK)
, MAGENTA_DRIVER_END(_driver_crypt)
