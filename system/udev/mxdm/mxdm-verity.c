// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/common/mxdm.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/ioctl.h>
#include <ddk/iotxn.h>
#include <lib/crypto/cryptolib.h>
#include <magenta/device/verity.h>

#define VERITY_DIGEST_LEN 32

typedef struct verity {
    verity_header_t header;
    mxdm_t* mxdm;
    mtx_t mtx;
    union {
        const uint8_t* digest;
        const clBignumModulus* public_key;
    } root;
    verity_mode_t mode;
    // Working space
    clSHA256_CTX hash_ctx;
    uint8_t expected[VERITY_DIGEST_LEN];
    uint8_t data[MXDM_BLOCK_SIZE];
} verity_t;

//

static verity_t* verity_from_mxdm(mxdm_t* mxdm) {
    return containerof(mxdm, verity_t, mxdm);
}

static verity_t* verity_from_worker(mxdm_worker_t* worker) {
    return verity_from_mxdm(mxdm_from_worker(worker));
}

static mx_status_t verity_get_offset(verity_t* verity, uint64_t blkoff,
                                     uint64_t* digest_off,
                                     uint64_t* digest_blkoff) {
    uint64_t* begins = verity->header.begins;
    uint64_t* ends = verity->header.ends;
    for (uint8_t depth = 0; depth < VERITY_MAX_DEPTH - 1; ++depth) {
        // Determine the current level.
        if (begins[depth] <= blkoff && blkoff < ends[depth]) {
            *digest_off = (blkoff - begins[depth]) * VERITY_DIGEST_LEN;
            *digest_blkoff =
                (*digest_off / MXDM_BLOCK_SIZE) + begins[depth + 1];
            return NO_ERROR;
        }
    }
    // We've ascended to the root of the hash tree.
    return ERR_CHECKSUM_FAIL;
}

static clSHA256_CTX* verity_hash_ctx(mxdm_worker_t* worker, uint64_t blkoff) {
    verity_t* verity = verity_from_worker(worker);
    verity_header_t* header = &verity->header;
    clSHA256_CTX* hash_ctx = &verity->hash_ctx;
    clSHA256_init(hash_ctx);
    uint8_t prefix = (mxdm_is_data(worker, blkoff) ? 0 : 1);
    clHASH_update(hash_ctx, &prefix, 1);
    clHASH_update(hash_ctx, header->salt, header->salt_len);
    clHASH_update(hash_ctx, verity->data, MXDM_BLOCK_SIZE);
    return hash_ctx;
}

static bool verity_check_digest(mxdm_worker_t* worker, uint64_t blkoff,
                                const uint8_t* expected) {
    if (!expected) {
        return false;
    }
    clSHA256_CTX* hash_ctx = verity_hash_ctx(worker, blkoff);
    const uint8_t* actual = clHASH_final(hash_ctx);
    return memcmp(actual, expected, VERITY_DIGEST_LEN) == 0;
}

static bool verity_check_signature(mxdm_worker_t* worker, uint64_t blkoff,
                                   const clBignumModulus* key) {
    if (!key) {
        return false;
    }
    verity_t* verity = verity_from_worker(worker);
    verity_header_t* header = &verity->header;
    clSHA256_CTX* hash_ctx = verity_hash_ctx(worker, blkoff);
    return clRSA2K_verify(key, header->signature, header->signature_len,
                          hash_ctx) == 1;
}

// Ioctls

static verity_mode_t verity_get_mode(verity_t* verity) {
    verity_mode_t mode;
    mtx_lock(&verity->mtx);
    mode = verity->mode;
    mtx_unlock(&verity->mtx);
    return mode;
}

static void verity_set_mode(verity_t* verity, verity_mode_t mode) {
    mtx_lock(&verity->mtx);
    verity->mode = mode;
    mtx_unlock(&verity->mtx);
}

static mx_status_t verity_set_root(verity_t* verity, const void* buf,
                                   size_t len) {
    mx_status_t rc = NO_ERROR;
    verity_header_t* header = &verity->header;
    if ((header->signature_len != 0 || len != VERITY_DIGEST_LEN) &&
        len != header->signature_len) {
        return ERR_INVALID_ARGS;
    }
    if (verity_get_mode(verity) != kVerityModeIgnore) {
        return ERR_BAD_STATE;
    }
    mtx_lock(&verity->mtx);
    if (header->signature_len != 0) {
        if (!verity->root.public_key) {
            rc = ERR_NOT_READY;
        } else {
            verity->root.public_key = buf;
        }
    } else {
        if (!verity->root.digest) {
            rc = ERR_NOT_READY;
        } else {
            verity->root.digest = buf;
        }
    }
    // If not ignoring, do a synchronous read to test the root value.
    if (rc == NO_ERROR && verity_get_mode(verity) != kVerityModeIgnore) {
        rc = mxdm_read(verity->mxdm, header->begins[0], verity->data,
                       MXDM_BLOCK_SIZE);
    }
    mtx_unlock(&verity->mtx);
    return rc;
}

static ssize_t verity_ioctl(mxdm_t* mxdm, uint32_t op, const void* in_buf,
                            size_t in_len, void* out_buf, size_t out_len) {
    verity_t* verity = verity_from_mxdm(mxdm);
    switch (op) {
    case IOCTL_VERITY_GET_MODE:
        if (!out_buf || out_len < sizeof(verity_mode_t)) {
            return ERR_NOT_ENOUGH_BUFFER;
        }
        verity_mode_t* out_mode = out_buf;
        *out_mode = verity_get_mode(verity);
        return sizeof(*out_mode);
    case IOCTL_VERITY_SET_MODE:
        if (!in_buf || in_len != sizeof(verity_mode_t)) {
            return ERR_INVALID_ARGS;
        }
        const verity_mode_t* in_mode = in_buf;
        verity_set_mode(verity, *in_mode);
        return NO_ERROR;
    case IOCTL_VERITY_SET_ROOT:
        if (!out_buf) {
            return ERR_INVALID_ARGS;
        }
        return verity_set_root(verity, in_buf, in_len);
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_status_t verity_prepare(mxdm_worker_t* worker, uint64_t blklen,
                                  uint64_t* data_blkoff,
                                  uint64_t* data_blklen) {
    mx_status_t rc = NO_ERROR;
    verity_t* verity = verity_from_worker(worker);
    // Initialize synchronization control
    if (mtx_init(&verity->mtx, mtx_plain) != thrd_success) {
        return ERR_NO_RESOURCES;
    }
    // Read the header from the first block on the device.
    verity_header_t* header = &verity->header;
    rc = mxdm_read(verity->mxdm, 0, header, sizeof(*header));
    if (rc < 0) {
        return rc;
    }
    // Check magic number and version.
    if (header->magic != VERITY_MAGIC ||
        header->version != VERITY_VERSION_1_0) {
        return ERR_NOT_SUPPORTED;
    }
    // Check header block digest and mark block as "verified" if it matches.
    memset(verity->data, 0, MXDM_BLOCK_SIZE);
    memcpy(verity->data, header, sizeof(*header));
    memset(verity->data + offsetof(verity_header_t, digest), 0,
           VERITY_DIGEST_LEN);
    if (!verity_check_digest(worker, 0, header->digest)) {
        return ERR_CHECKSUM_FAIL;
    }
    // Do a basic validation of the tree structure.
    const uint64_t* begins = header->begins;
    const uint64_t* ends = header->ends;
    for (uint8_t i = 0; i < VERITY_MAX_DEPTH; ++i) {
        // All levels must be well-formed and fit on the device
        if (begins[i] >= ends[i] || ends[i] > blklen) {
            return ERR_BAD_STATE;
        }
        // Ranges must not overlap
        for (uint8_t j = 0; j < VERITY_MAX_DEPTH; ++j) {
            if (i != j && begins[i] < ends[j] && begins[j] < ends[i]) {
                return ERR_BAD_STATE;
            }
        }
        // Each non-leaf level must be able to hold digests the level below it
        if (i != 0 &&
            (ends[i] - begins[i]) * MXDM_BLOCK_SIZE <
                (ends[i - 1] - begins[i - 1]) * VERITY_DIGEST_LEN) {
            return ERR_BAD_STATE;
        }
    }
    *data_blkoff = begins[0];
    *data_blklen = ends[0] - begins[0];
    return NO_ERROR;
}

static mx_status_t verity_release(mxdm_worker_t* worker) {
    verity_t* verity = verity_from_worker(worker);
    free(verity);
    return NO_ERROR;
}

static mxdm_txn_action_t verity_before_write(mxdm_worker_t* worker,
                                             iotxn_t* txn, uint64_t* blkoff,
                                             uint64_t blkmax) {
    txn->status = ERR_ACCESS_DENIED;
    return kMxdmCompleteTxn;
}

static mxdm_txn_action_t verity_after_read(mxdm_worker_t* worker, iotxn_t* txn,
                                           uint64_t* blkoff, uint64_t blkmax) {
    verity_t* verity = verity_from_worker(worker);
    verity_header_t* header = &verity->header;
    uint64_t root_offset = header->begins[header->depth - 1];
    bool trusted_root =
        mxdm_check_blocks(worker, &root_offset, root_offset + 1);
    // If we're ignoring everything, just complete the txn.
    if (verity_get_mode(verity) == kVerityModeIgnore) {
        return kMxdmCompleteTxn;
    }
    // If this block is an already verified root, we're done.
    if (*blkoff == root_offset && trusted_root) {
        return kMxdmCompleteTxn;
    }
    // If this is an unverified root, try to verify it.
    if (*blkoff == root_offset) {
        txn->ops->copyfrom(txn, verity->data, 0, MXDM_BLOCK_SIZE);
        // Extract the saved verification buffer.
        mtx_lock(&verity->mtx);
        // Check the digest or signature.
        if (header->signature_len == 0) {
            if (verity_check_digest(worker, 0, verity->root.digest)) {
                mxdm_mark_block(worker, *blkoff);
            }
            verity->root.digest = NULL;
        } else {
            if (verity_check_signature(worker, 0, verity->root.public_key)) {
                mxdm_mark_block(worker, *blkoff);
            }
            verity->root.public_key = NULL;
        }
        mtx_unlock(&verity->mtx);
        return kMxdmCompleteTxn;
    }
    // If the root hasn't been verified, other blocks can't be verified.
    if (!trusted_root) {
        txn->status = ERR_CHECKSUM_FAIL;
        return kMxdmCompleteTxn;
    }
    // Check that each block's digest matches the one in the layer above.
    uint64_t digest_off, digest_blkoff = 0;
    while (!mxdm_check_blocks(worker, blkoff, blkmax)) {
        txn->status =
            verity_get_offset(verity, *blkoff, &digest_off, &digest_blkoff);
        if (digest_blkoff == 0) {
            break;
        }
        mxdm_block_t* block = NULL;
        if (!mxdm_acquire_block(worker, digest_blkoff, &block)) {
            return kMxdmRequeueTxn;
        }
        if (!mxdm_block_is_ready(block)) {
            mxdm_wait_for_block(block, txn);
            return kMxdmIgnoreTxn;
        }
        mxdm_get_block(block, digest_off, VERITY_DIGEST_LEN, verity->expected);
        mxdm_release_block(worker, block);
        // Calculate the actual digest and compare
        uint64_t offset = (*blkoff * MXDM_BLOCK_SIZE) - txn->offset;
        txn->ops->copyfrom(txn, verity->data, offset, MXDM_BLOCK_SIZE);
        if (!verity_check_digest(worker, *blkoff, verity->expected)) {
            txn->status = ERR_CHECKSUM_FAIL;
            break;
        }
        mxdm_mark_block(worker, *blkoff);
    }
    return kMxdmCompleteTxn;
}

//

static mxdm_ops_t verity_ops = {
    .prepare = verity_prepare,
    .release = verity_release,
    .ioctl = verity_ioctl,
    .before_write = verity_before_write,
    .after_read = verity_after_read,
};

static mx_status_t verity_bind(mx_driver_t* drv, mx_device_t* parent) {
    return mxdm_init(drv, parent, "verity", &verity_ops, sizeof(verity_t));
}

static mx_bind_inst_t binding[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_BLOCK),
};

mx_driver_t _driver_verity BUILTIN_DRIVER = {
    .name = "mxdm-verity",
    .ops =
        {
            .bind = verity_bind,
        },
    .binding = binding,
    .binding_size = sizeof(binding),
};
