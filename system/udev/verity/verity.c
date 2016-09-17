// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODOs:
//  same/diff devs
//  excl open
//  add/remove magic and rebind
//  hash or sig
//  superblock in general

#include <ddk/common/filter.h>

#include <ddk/binding.h>
#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/block.h>

#include <assert.h>
#include <lib/crypto/cryptolib.h>
#include <magenta/device/verity.h>
#include <magenta/listnode.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>

#include "bitmap.h"
#include "worker.h"

// Macros

#define VERITY_DIGESTS_PER_BLOCK (VERITY_BLOCK_SIZE / VERITY_DIGEST_LEN)
#define VERITY_VERIFIER_THREADS 1
#define VERITY_DIGESTER_THREADS 1

#define verity_lba(offset) ((offset) / VERITY_BLOCK_SIZE)
#define verity_blklen(start, end, blksize) ((end - 1) / blksize - (start) / blksize + 1)
#define verity_txnoff(txn) verity_lba(txn->offset)
#define verity_txnlen(txn) verity_blklen(txn->offset, txn->offset + txn->length, VERITY_BLOCK_SIZE)

// Types

typedef struct verity {
    verity_header_t* header;
    bitmap_t* bitmap;

    filter_t* filter;
    filter_worker_t* verifier;
    filter_worker_t* digester;

    uint64_t corrupt;
    verity_mode_t mode;
    mtx_t mtx;

} verity_t;

static void verity_free(verity_t* verity) {
    if (verity->header) {
        free(verity->header);
    }
    if (verity->bitmap) {
        bitmap_free(verity->bitmap);
    }
    free(verity);
}

static verity_t* verity_get(filter_t* filter) {
    return containerof(filter, verity_t, filter);
}

// Synchronous I/O subroutines

static void verity_read_block_cb(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

static iotxn_t* verity_read_block(mx_device_t* parent, uint64_t lba) {
    iotxn_t* txn;
    if (iotxn_alloc(&txn, 0, VERITY_BLOCK_SIZE, 0) != NO_ERROR) {
        return NULL;
    }
    completion_t completion = COMPLETION_INIT;
    txn->opcode = IOTXN_OP_READ;
    txn->offset = lba * VERITY_BLOCK_SIZE;
    txn->length = VERITY_BLOCK_SIZE;
    txn->complete_cb = verity_read_block_cb;
    txn->cookie = &completion;
    parent->ops->iotxn_queue(parent, txn);
    completion_wait(&completion, MX_TIME_INFINITE);
    return txn;
}

// Crypto subroutines

void verity_digest(const uint8_t* data, size_t len, uint8_t* out_digest) {
    clSHA256(data, len, out_digest);
}

static bool verity_digest_compare(verity_header_t* header, uint64_t lba, iotxn_t* txn, const uint8_t* expected) {
    clSHA256_CTX ctx = { 0 };
    clHASH_init(&ctx);
    // Prevent second pre-image attacks and use salts when hashing.
    uint8_t one = (header->begins[0] <= lba && lba < header->ends[0] ? 0 : 1);
    clHASH_update(&ctx, &one, 1);
    clHASH_update(&ctx, header->salt, header->salt_len);
    // Hash the block data
    uint8_t data[VERITY_BLOCK_SIZE];
    uint64_t off = (lba - verity_txnoff(txn)) * VERITY_BLOCK_SIZE;
    txn->ops->copyfrom(txn, data, VERITY_BLOCK_SIZE, off);
    clHASH_update(&ctx, data, VERITY_BLOCK_SIZE);
    // Does it match?
    const uint8_t* actual = clHASH_final(&ctx);
    return memcmp(actual, expected, VERITY_DIGEST_LEN) == 0;
}

static bool verity_signature_verify(verity_header_t* header, iotxn_t* txn, const uint8_t* key, size_t len) {
    // Check that the key looks right.
    if (len != sizeof(clBignumModulus)) {
        return false;
    }
    uint8_t key_digest[VERITY_DIGEST_LEN];
    clSHA256(key, sizeof(clBignumModulus), key_digest);
    if (memcmp(header->key_digest, key_digest, VERITY_DIGEST_LEN) != 0) {
        return false;
    }
    // Get the digest of the block.
    clSHA256_CTX ctx;
    clHASH_init(&ctx);
    uint8_t data[VERITY_BLOCK_SIZE];
    txn->ops->copyfrom(txn, data, VERITY_BLOCK_SIZE, 0);
    clHASH_update(&ctx, data, VERITY_BLOCK_SIZE);
    // Check the signature.
    clBignumModulus* k = (clBignumModulus*)key;
    return clRSA2K_verify(k, header->signature, header->signature_len, &ctx);
}

// IOCTL subroutines

static ssize_t verity_get_status(verity_t* verity, verity_status_msg_t* status_msg) {
    verity_header_t* header = verity->header;
    status_msg->lba = header->begins[0];
    if (!bitmap_check_one(verity->bitmap, 0)) {
        // Hash tree root hasn't been verified.
        status_msg->state = kVerityStateUnverified;

    } else if (header->begins[0] <= verity->corrupt && verity->corrupt < header->ends[0]) {
        // At least one leaf digest failed verification.
        status_msg->state = kVerityStateCorrupted;
        status_msg->lba = verity->corrupt;

    } else if (verity->corrupt != 0) {
        // At least one node digest failed verification.
        status_msg->state = kVerityStateCorrupted;
        status_msg->lba = header->ends[0];

    } else if (!bitmap_check_all(verity->bitmap, &status_msg->lba, header->ends[0])) {
        // At least one leaf digest hasn't been verified.
        status_msg->state = kVerityStatePartiallyVerified;

    } else {
        // Everything is verified!
        status_msg->state = kVerityStateFullyVerified;
    }
    // Index relative start of leaves.
    status_msg->lba -= header->begins[0];
    return sizeof(*status_msg);
}

static verity_mode_t verity_get_mode(verity_t* verity) {
    verity_mode_t mode;
    mtx_lock(&verity->mtx);
    mode = verity->mode;
    mtx_unlock(&verity->mtx);
    return mode;
}

static ssize_t verity_verify_root(verity_t* verity, const uint8_t* buf, size_t len) {
    verity_header_t* header = verity->header;
    // Read the top level of the tree.
    uint64_t lba = header->begins[header->depth - 1];
    mx_device_t* dev = filter_dev(verity->filter);
    iotxn_t* txn = verity_read_block(dev->parent, lba);
    if (header->signature_len == 0) {
        // Check the root digest
        if (len != VERITY_DIGEST_LEN) {
            return ERR_INVALID_ARGS;
        }
        if (!verity_digest_compare(header, 0, txn, buf)) {
            return ERR_CHECKSUM_FAIL;
        }
    } else {
        // Check the root signature
        if (len != header->signature_len) {
            return ERR_INVALID_ARGS;
        }
        if (!verity_signature_verify(header, txn, buf, len)) {
            return ERR_CHECKSUM_FAIL;
        }
    }
    bitmap_set_one(verity->bitmap, 0);
    return NO_ERROR;
}

static void verity_set_mode(verity_t* verity, verity_mode_t mode) {
    mtx_lock(&verity->mtx);
    verity->mode = mode;
    mtx_unlock(&verity->mtx);
}

// Protocol subroutines

static mx_status_t verity_release(filter_t* filter) {
    verity_free(verity_get(filter));
    return NO_ERROR;
}

static mx_status_t verity_validate_iotxn(iotxn_t* cloned) {
    filter_t* filter = filter_from_cloned(cloned);
    verity_t* verity = verity_get(filter);
    verity_header_t* header = verity->header;
    uint64_t end = header->ends[0] - header->begins[0];
    uint64_t off = verity_txnoff(cloned);
    uint64_t len = verity_txnlen(cloned);
    if (cloned->offset % VERITY_BLOCK_SIZE != 0 || cloned->length % VERITY_BLOCK_SIZE != 0 || off >= end) {
        return ERR_INVALID_ARGS;
    }
    // Move offset relative to start of leaves and constrain length.
    cloned->offset = (header->begins[0] + off) * VERITY_BLOCK_SIZE;
    cloned->length = MIN(end - off, len) * VERITY_BLOCK_SIZE;
    return NO_ERROR;
}

static mx_off_t verity_get_size(filter_t* filter, mx_off_t parent_size) {
    verity_t* verity = verity_get(filter);
    verity_header_t* header = verity->header;
    return (header->ends[0] - header->begins[0]) * VERITY_BLOCK_SIZE;
}

static ssize_t verity_ioctl(filter_t* filter, uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max) {
    verity_t* verity = verity_get(filter);
    verity_header_t* header = verity->header;

    switch (op) {
    case IOCTL_BLOCK_GET_SIZE: {
        uint64_t* size = reply;
        if (!size || max < sizeof(*size)) {
            return ERR_NOT_ENOUGH_BUFFER;
        }
        *size = verity_get_size(filter, 0);
        return sizeof(*size);
    }
    case IOCTL_BLOCK_GET_BLOCKSIZE: {
        uint64_t* blksize = reply;
        if (!blksize || max < sizeof(*blksize)) {
            return ERR_NOT_ENOUGH_BUFFER;
        }
        *blksize = VERITY_BLOCK_SIZE;
        return sizeof(*blksize);
    }
    case IOCTL_VERITY_GET_HEADER: {
        if (!reply || max < sizeof(*header)) {
            return ERR_NOT_ENOUGH_BUFFER;
        }
        memcpy(reply, header, sizeof(*header));
        return sizeof(*header);
    }
    case IOCTL_VERITY_GET_STATUS: {
        verity_status_msg_t* status_msg = reply;
        if (!reply || max < sizeof(*status_msg)) {
            return ERR_NOT_ENOUGH_BUFFER;
        }
        return verity_get_status(verity, status_msg);
    }
    case IOCTL_VERITY_GET_MODE: {
        if (!reply || max < sizeof(verity_mode_t)) {
            return ERR_NOT_ENOUGH_BUFFER;
        }
        verity_mode_t* mode = reply;
        *mode = verity_get_mode(verity);
        return sizeof(*mode);
    }
    case IOCTL_VERITY_SET_ROOT: {
        if (!reply) {
            return ERR_INVALID_ARGS;
        }
        return verity_verify_root(verity, cmd, cmdlen);
    }
    case IOCTL_VERITY_SET_MODE: {
        if (!cmd || cmdlen != sizeof(verity_mode_t)) {
            return ERR_INVALID_ARGS;
        }
        const verity_mode_t* mode = cmd;
        verity_set_mode(verity, *mode);
        return NO_ERROR;
    }
    default: {
        return ERR_NOT_SUPPORTED;
    }
    }
}

static filter_ops_t verity_protocol = {
    .release = verity_release,
    .validate_iotxn = verity_validate_iotxn,
    .get_size = verity_get_size,
    .ioctl = verity_ioctl,
};

// verity_bind subroutines

static mx_status_t verity_init_bitmap(verity_t* verity, mx_device_t* parent) {
    assert(!verity->bitmap);
    // Get and check device and block sizes for the parent device.
    size_t size;
    mx_status_t rc = parent->ops->ioctl(parent, IOCTL_BLOCK_GET_SIZE, NULL, 0, &size, sizeof(size));
    if (rc < 0) {
        return rc;
    }
    size_t blksize;
    rc = parent->ops->ioctl(parent, IOCTL_BLOCK_GET_BLOCKSIZE, NULL, 0, &blksize, sizeof(blksize));
    if (rc < 0) {
        return rc;
    }
    if (VERITY_BLOCK_SIZE % blksize != 0) {
        return ERR_NOT_SUPPORTED;
    }
    // Set up bitmap for tracking verified blocks.  It's roughly 128 KB per GB of storage.
    verity->bitmap = bitmap_init(size / VERITY_BLOCK_SIZE);
    if (!verity->bitmap) {
        return ERR_NO_RESOURCES;
    }
    return NO_ERROR;
}

static mx_status_t verity_init_header(verity_t* verity, mx_device_t* parent) {
    assert(!verity->header);
    mx_status_t rc = NO_ERROR;
    // Read the header from the first block on the device.
    iotxn_t* txn = verity_read_block(parent, 0);
    verity->header = calloc(1, sizeof(verity_header_t));
    verity_header_t* header = verity->header;
    if (!txn || !header) {
        rc = ERR_NO_MEMORY;
        goto done;
    }
    if (txn->status < 0) {
        rc = txn->status;
        goto done;
    }
    if (txn->actual < sizeof(*header)) {
        rc = ERR_INTERNAL;
        goto done;
    }
    txn->ops->copyfrom(txn, header, sizeof(*header), 0);
    // Check magic number and version.
    if (header->magic != VERITY_MAGIC) {
        rc = ERR_NOT_SUPPORTED;
        goto done;
    }
    if (header->version != VERITY_VERSION_1_0) {
        rc = ERR_NOT_SUPPORTED;
        goto done;
    }
    // Check header block digest and mark block as "verified" if it matches.
    uint8_t digest[VERITY_DIGEST_LEN];
    memcpy(digest, header->digest, VERITY_DIGEST_LEN);
    memset(header->digest, 0, VERITY_DIGEST_LEN);
    if (!verity_digest_compare(header, 0, txn, digest)) {
        rc = ERR_CHECKSUM_FAIL;
    }
    memcpy(header->digest, digest, VERITY_DIGEST_LEN);
    if (rc < 0) {
        goto done;
    }
    bitmap_set_one(verity->bitmap, 0);
    // Do a basic validation of the tree structure.
    uint64_t num_blocks = bitmap_len(verity->bitmap);
    const uint64_t* begins = header->begins;
    const uint64_t* ends = header->ends;
    for (uint8_t i = 0; i < VERITY_MAX_DEPTH; ++i) {
        // All levels must be well-formed and fit on the device
        if (begins[i] >= ends[i] || ends[i] > num_blocks) {
            rc = ERR_BAD_STATE;
            goto done;
        }
        // Ranges must not overlap
        for (uint8_t j = 0; j < VERITY_MAX_DEPTH; ++j) {
            if (i == j) {
                continue;
            }
            if (begins[i] < ends[j] && begins[j] < ends[i]) {
                rc = ERR_BAD_STATE;
                goto done;
            }
        }
        // Each non-leaf level must be able to hold digests the level below it
        if (i == 0) {
            continue;
        }
        if (ends[i] - begins[i] < verity_blklen(begins[i - 1], ends[i - 1], VERITY_DIGESTS_PER_BLOCK)) {
            rc = ERR_BAD_STATE;
            goto done;
        }
    }
    rc = NO_ERROR;
done:
    if (txn) {
        txn->ops->release(txn);
    }
    return rc;
}

// I/O workers

static void verity_node_cb(iotxn_t* node, void* cookie) {
    iotxn_t* cloned = cookie;
    filter_t* filter = filter_from_cloned(cloned);
    verity_t* verity = verity_get(filter);
    filter_assign(node, verity->digester, true /* skip_validation */);
}

static void verity_verifier(iotxn_t* cloned) {
    filter_t* filter = filter_from_cloned(cloned);
    verity_t* verity = verity_get(filter);
    verity_header_t* header = verity->header;
    uint64_t off = verity_txnoff(cloned);
    uint64_t len = verity_txnlen(cloned);
    // As the default worker, we may get handed write transactions if we're in bypass mode. Clear any modified blocks.
    if (cloned->opcode == IOTXN_OP_WRITE) {
        if (verity_get_mode(verity) != kVerityModeBypassing) {
            cloned->status = ERR_ACCESS_DENIED;
            goto done;
        }
        // On one hand, these blocks are no longer verified.  On the other, there's a chance the caller is fixing a corrupted block.
        bitmap_clear_all(verity->bitmap, off, len);
        if (off <= verity->corrupt && verity->corrupt < off + len) {
            verity->corrupt = 0;
        }
        goto done;
    }
    // Otherwise, check if all the read blocks have been previously verified.
    uint64_t tmp = off;
    if (bitmap_check_all(verity->bitmap, &tmp, len)) {
        goto done;
    }
    // If not, we need to read a node of the hash tree.
    iotxn_t* node = NULL;
    cloned->status = iotxn_alloc(&node, 0, VERITY_BLOCK_SIZE, 0);
    if (cloned->status != NO_ERROR) {
        goto done;
    }
    // Determine the digest blocks we need.
    uint8_t i = 0;
    for (; i < VERITY_MAX_DEPTH - 1; ++i) {
        if (header->begins[i] <= off && off < header->ends[i]) {
            break;
        }
    }
    // A top-level block needs a previous call to verity_verify_root.
    if (i == VERITY_MAX_DEPTH - 1) {
        cloned->status = ERR_CHECKSUM_FAIL;
        goto done;
    }
    // Adjust off, len for the next level up the tree.
    len = verity_blklen(off, len, VERITY_DIGESTS_PER_BLOCK);
    off -= header->begins[i];
    off /= VERITY_DIGESTS_PER_BLOCK;
    off += header->begins[i + 1];
    // Configure the iotxn to read the digest blocks.
    node->offset = off * VERITY_BLOCK_SIZE;
    node->length = len * VERITY_BLOCK_SIZE;
    node->protocol = cloned->protocol;
    node->complete_cb = verity_node_cb;
    node->cookie = cloned;
    filter_assign(node, verity->verifier, true /* skip_validation */);
    return;
done:
    if (node) {
        node->ops->release(node);
    }
    filter_complete(cloned);
}

// Digester thread

static void verity_digester(iotxn_t* cloned) {
    if (cloned->status != NO_ERROR) {
        goto done;
    }
    filter_t* filter = filter_from_cloned(cloned);
    verity_t* verity = verity_get(filter);
    verity_mode_t mode = verity_get_mode(verity);
    iotxn_t* node = cloned->cookie;
    iotxn_t* prev = node->cookie;
    uint64_t off = verity_txnoff(prev);
    uint64_t len = verity_txnoff(prev);
    // Iterate over each block previously read and match it to a digest.
    for (uint64_t i = off % VERITY_DIGESTS_PER_BLOCK; off < len; ++off, ++i) {
        // Get the digest data.
        uint8_t digest[VERITY_DIGEST_LEN];
        node->ops->copyfrom(node, digest, VERITY_DIGEST_LEN, i * VERITY_DIGEST_LEN);
        // Verify the digest and remember or fail.
        if (verity_digest_compare(verity->header, off, prev, digest)) {
            bitmap_set_one(verity->bitmap, off);
        } else if (mode == kVerityModeEnforcing) {
            cloned->status = ERR_CHECKSUM_FAIL;
            break;
        }
    }
done:
    filter_complete(cloned);
    return;
}

static mx_status_t verity_bind(mx_driver_t* drv, mx_device_t* parent) {
    mx_ssize_t rc = NO_ERROR;
    // Initialize verity and filter structures.
    verity_t* verity = calloc(1, sizeof(verity_t));
    if (!verity) {
        rc = ERR_NO_MEMORY;
        goto fail;
    }
    char name[20]; // TODO: how long is dev->name?
    snprintf(name, sizeof(name), "%s-verity", parent->name);
    verity->filter = filter_init(drv, name, MX_PROTOCOL_BLOCK, &verity_protocol);
    if (!verity->filter) {
        rc = ERR_NO_RESOURCES;
        goto fail;
    }
    // Set up verity device.
    if (mtx_init(&verity->mtx, mtx_plain) != thrd_success) {
        rc = ERR_NO_RESOURCES;
        goto fail;
    }
    rc = verity_init_header(verity, parent);
    if (rc != NO_ERROR) {
        goto fail;
    }
    rc = verity_init_bitmap(verity, parent);
    if (rc != NO_ERROR) {
        goto fail;
    }
    verity_set_mode(verity, kVerityModeEnforcing);
    // Add the workers.
    verity->verifier = filter_add_worker(verity->filter, verity_verifier, VERITY_VERIFIER_THREADS, true /* is_default */);
    if (!verity->verifier) {
        rc = ERR_NO_RESOURCES;
        goto fail;
    }
    verity->digester = filter_add_worker(verity->filter, verity_digester, VERITY_DIGESTER_THREADS, false /* not is_default */);
    if (!verity->digester) {
        rc = ERR_NO_RESOURCES;
        goto fail;
    }
    // Bind the device to the parent in devmgr's device tree.
    rc = filter_add(verity->filter, parent);
    if (rc < 0) {
        goto fail;
    }
    return NO_ERROR;
fail:
    // If we ended up here, the device was not bound; we can just release.
    if (verity) {
        verity_free(verity);
    }
    return rc;
}

static mx_bind_inst_t binding[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_BLOCK),
};

mx_driver_t _driver_verity BUILTIN_DRIVER = {
    .name = "verity",
    .ops = {
        .bind = verity_bind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
