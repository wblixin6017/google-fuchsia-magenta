// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODOs:
//  same/diff devs
//  excl open
//  add/remove magic and rebind
//  hash or sig
//  superblock in general

#include <ddk/binding.h>
#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/block.h>

#include <assert.h>
#include <lib/crypto/cryptolib.h>
#include <magenta/listnode.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>

// Constants

#define VERITY_DIGEST_LEN 32
#define VERITY_BLOCK_SIZE 512
#define VERITY_DIGESTS_PER_BLOCK (VERITY_BLOCK_SIZE / VERITY_DIGEST_LEN)
#define VERITY_VERIFIER_THREADS 1
#define VERITY_DIGESTER_THREADS 1

#define TRACE 1

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

// Types

// magic | version | signature

typedef enum verity_mode {
    VERITY_BYPASS,
    VERITY_IGNORE,
    VERITY_ENFORCE,
    VERITY_SHUTDOWN,
} verity_mode_t;

typedef uint64_t lba_t;

typedef struct level_t {
    list_node_t node;
    lba_range_t range;
}

typedef struct verity_device {
    mx_device_t dev;
    lba_t num_leaves;
    lba_t num_blocks;

    verity_mode_t mode;
    mtx_t mode_mtx;

    uint64_t* bitmap;
    size_t bitmap_len;
    mtx_t bitmap_mtx;

    list_node_t iotxns;
    mtx_t iotxns_mtx;

    list_node_t to_verify;
    mtx_t verifier_mtx;
    cnd_t verifier_cnd;

    list_node_t to_digest;
    mtx_t digester_mtx;
    cnd_t digester_cnd;

    thrd_t threads[VERITY_VERIFIER_THREADS + VERITY_DIGESTER_THREADS];
    size_t num_threads;

    list_node_t levels;
} verity_device_t;






typedef verity_superblock {
    uint64_t magic;
    uint32_t revision;

    // TODO: ref to hash_device
    uint8_t reserved_uuid[16];
    uint32_t reserved_hash_blksize;
    uint32_t reserved_data_blksize;

    lba_range_t levels[8];
    uint8_t levels_len;

    // https://bugs.chromium.org/p/chromium/issues/detail?id=194620
    uint8_t salt[256]; 
    uint16_t salt_len;

    uint8_t reserved_algorithm_id[32];
    uint8_t key_id[VERITY_DIGEST_LEN];
    uint8_t signature[512];
    uint16_t signature_len;

    uint8_t reserved[0];
} verity_superblock_t



typedef struct lba_range {
    lba_t offset;
    lba_t length;
} lba_range_t;


static verity_device_t* verity_get_device(mx_device_t* dev) {
    return containerof(dev, verity_device_t, dev);
}

// Bitmap operations

static bool verity_check_bit(verity_device_t* device, lba_t offset) {
    bool is_set = false;
    mtx_lock(&device->bitmap_mtx);
    is_set = (device->bitmap[offset / 64] & 1ULL << (63 - (offset % 64)));
    mtx_unlock(&device->bitmap_mtx);
    return is_set;
}

static bool verity_check_all(verity_device_t* device, lba_t* off, lba_t max) {
    lba_t i = *off / 64;
    lba_t n = max / 64;
    if (n > device->bitmap_len) {
        n = device->bitmap_len;
    }
    mtx_lock(&device->bitmap_mtx);
    lba_t count = __builtin_clz(~device->bitmap[i++] << (*off % 64));
    while (count == 64 && i <= n) {
        count = __builtin_clz(~device->bitmap[i++]);
    }
    mtx_unlock(&device->bitmap_mtx);
    *off = (((i - 1) * 64) - *off);
    if (*off > max) {
        *off = max;
    }
    return *off == max;
}

static void verity_set_bit(verity_device_t* device, lba_t off) {
    mtx_lock(&device->bitmap_mtx);
    device->bitmap[off / 64] |= 1ULL << (63 - (off % 64));
    mtx_unlock(&device->bitmap_mtx);
}

static void verity_clear_all(verity_device_t* device, lba_t off, lba_t max) {
    lba_t i = off / 64;
    lba_t n = max / 64;
    if (n > device->bitmap_len) {
        n = device->bitmap_len;
    }
    mtx_lock(&device->bitmap_mtx);
    if (off % 64 != 0) {
        device->bitmap[i++] &= (~0ULL) << (off % 64);
    }
    while (i < max / 64) {
        device->bitmap[i++] = 0;
    }
    if (max % 64 != 0) {
        device->bitmap[i] &= (~0ULL) >> (max % 64);
    }
    mtx_unlock(&device->bitmap_mtx);
}




// List operations

static void verity_take(verity_device_t* device, iotxn_t* txn) {
    mtx_lock(&device->iotxns_mtx);
    list_add_tail(&device->iotxns, &txn->node);
    mtx_unlock(&device->iotxns_mtx);
    txn->context = device;
}

static void verity_yield(verity_device_t* device, iotxn_t* txn) {
    mtx_lock(&device->iotxns_mtx);
    list_delete(&txn->node);
    mtx_unlock(&device->iotxns_mtx);
    txn->context = NULL;
}



// Tree operations

static uint8_t verity_get_depth(const verity_device_t* device, lba_t offset) {
    lba_range_t *range = NULL;
    for(uint8_t i = 0; i < device->tree_height; ++i) {
        range = &device->tree_ranges[i];
        if(range->offset <= offset && offset < range->offset + range->length) {
            return i;
        }
    }
    xprintf("%s: error: offset %lu is out of range.\n", device->dev.name, offset);
    return device->tree_height;
}


void arvhice(){
    lba_t base = 0;
    lba_t len = device->num_leaves;
    while (base + len - 1 < offset) {
        base += len;
        len = ((len - 1) / VERITY_DIGESTS_PER_BLOCK) + 1;
    }
    if (start) {
        *start = base;
    }
    if (end) {
        *end = base + len;
    }
}



static lba_t verity_get_parent_node(const verity_device_t* device, lba_t lba) {
    uint8_t depth = verity_get_depth(device, lba);
    if (depth == 0) {
        return 0;
    }
    lba -= device->tree_ranges[depth].offset;
    lba /= VERITY_DIGESTS_PER_BLOCK;
    lba += device->tree_ranges[depth - 1].offset;
    return lba;
}










static lba_t verity_tree_size(lba_t blocks) {
    if (blocks < 2) {
        return 0;
    }
    lba_t tree = 0;
    lba_t len = blocks;
    while (len > 1) {
        tree += len;
        len = ((len - 1) / VERITY_DIGESTS_PER_BLOCK) + 1;
    }
    return tree - blocks;
}

static lba_t verity_get_max_leaves(size_t size) {
    lba_t blocks = size / VERITY_BLOCK_SIZE;
    // First find the max tree size; that is, the number of nodes if every block
    // is a leaf.
    lba_t max = verity_tree_size(blocks);
    // Next find the tree size for the "guaranteed safe" number of leaves, which
    // is the number of blocks minus the max tree size.
    lba_t safe = verity_tree_size(blocks - max);
    // The  difference in the number of nodes between the "safe" size and the
    // optimal size is small enough that a simple brute force search works.
    lba_t leaves;
    for (leaves = safe + 1; leaves < max; ++leaves) {
        if (leaves + verity_tree_size(leaves) > blocks) {
            --leaves;
            break;
        }
    }
    return leaves;
}




// Mode operations

static verity_mode_t verity_get_mode(verity_device_t* device) {
    verity_mode_t mode;
    mtx_lock(&device->mode_mtx);
    mode = device->mode;
    mtx_unlock(&device->mode_mtx);
    return mode;
}

static void verity_set_mode(verity_device_t* device, verity_mode_t mode) {
    mtx_lock(&device->mode_mtx);
    device->mode = mode;
    mtx_unlock(&device->mode_mtx);
}

// Callbacks

static void verity_verifier_cb(iotxn_t* txn, void* cookie) {
    iotxn_t* prev = cookie;
    verity_device_t* device = prev->context;
    if (!device) {
        // TODO: verity_failure?
        xprintf("verity: error %d: device released!\n", ERR_NOT_FOUND);
        prev->ops->complete(prev, ERR_NOT_FOUND, 0);
        txn->ops->release(txn);
        return;
    }
    txn->context = prev;
    mtx_lock(&device->verifier_mtx);
    list_add_tail(&device->to_verify, &txn->node);
    cnd_broadcast(&device->verifier_cnd);
    mtx_unlock(&device->verifier_mtx);
}

static void verity_digester_cb(iotxn_t* txn, void* cookie) {
    iotxn_t* prev = cookie;
    verity_device_t* device = prev->context;
    txn->context = prev;
    if (!device) {
        // TODO: verity_failure?
        xprintf("verity: error %d: device released!\n", ERR_NOT_FOUND);
        prev->ops->complete(prev, ERR_NOT_FOUND, 0);
        txn->ops->release(txn);
        return;
    }
    mtx_lock(&device->digester_mtx);
    list_add_tail(&device->to_digest, &txn->node);
    cnd_broadcast(&device->digester_cnd);
    mtx_unlock(&device->digester_mtx);
}

// Verifier thread

static void verity_queue_verified_read(verity_device_t* device, iotxn_t* txn) {
    iotxn_t* next = NULL;
    mx_status_t status = txn->ops->clone(txn, &next, 0);
    if (status != NO_ERROR) {
        xprintf("%s: error %d cloning iotxn\n", device->dev.name, status);
        txn->ops->complete(txn, status, 0);
        return;
    }
    verity_take(device, txn);
    next->complete_cb = verity_verifier_cb;
    next->cookie = txn;
    mx_device_t* parent = device->dev.parent;
    parent->ops->iotxn_queue(parent, next);
}

// TODO: handle "actual iotxns"
// TODO: make device const
static int verity_verifier_thread(void* args) {
    verity_device_t* device = args;
    while (true) {
        // Get the next read response to be verified.
        mtx_lock(&device->verifier_mtx);
        do {
            if (list_is_empty(&device->to_verify)) {
                cnd_wait(&device->verifier_cnd, &device->verifier_mtx);
            }
            if (verity_get_mode(device) == VERITY_SHUTDOWN) {
                thrd_exit(EXIT_SUCCESS);
            }
        } while (list_is_empty(&device->to_verify));
        iotxn_t* txn = list_remove_head_type(&device->to_verify, iotxn_t, node);
        mtx_unlock(&device->verifier_mtx);
        iotxn_t* prev = txn->context;
        txn->context = NULL;
        // Check for I/O error from parent device.
        lba_t off = txn->offset / VERITY_BLOCK_SIZE;
        if (txn->status != NO_ERROR) {
            xprintf("%s: error %d reading LBA %llu\n", device->dev.name, txn->status, off);
            verity_yield(device, prev);
            prev->ops->complete(prev, txn->status, 0);
            txn->ops->release(txn);
            continue;
        }
        // Check if all the read blocks have been previously verified.
        lba_t end = ((txn->offset + txn->actual - 1) / VERITY_BLOCK_SIZE) + 1;
        verity_check_all(device, &off, end);
        size_t actual = txn->actual;
        txn->ops->release(txn);
        // If we reached the end, everything is verified.
        if (off == end) {
            xprintf("%s: verified iotxn!\n", device->dev.name);
            verity_yield(device, prev);
            prev->ops->complete(prev, NO_ERROR, actual);
            continue;
        }
        // Otherwise, we need to read a digest block.
        iotxn_t* next;
        mx_status_t status = iotxn_alloc(&next, 0, VERITY_BLOCK_SIZE, 0);
        if (status != NO_ERROR) {
            xprintf("%s: error %d allocating iotxn\n", device->dev.name, status);
            verity_yield(device, prev);
            prev->ops->complete(prev, status, 0);
            continue;
        }
        next->offset = verity_parent_node(device, off) * VERITY_BLOCK_SIZE;
        next->length = VERITY_BLOCK_SIZE;
        next->protocol = prev->protocol;
        next->complete_cb = verity_digester_cb;
        next->cookie = prev;
        verity_queue_verified_read(device, next);
    }
    return NO_ERROR;
}

// Digester thread

static mx_ssize_t verity_match_digest(const uint8_t* expected, iotxn_t* txn, lba_t off, bool is_leaf) {
    clSHA256_CTX ctx;
    uint8_t is_nonleaf = (is_leaf ? 0 : 1);
    uint8_t block[VERITY_BLOCK_SIZE];
    txn->ops->copyfrom(txn, &block, VERITY_BLOCK_SIZE, off * VERITY_BLOCK_SIZE);
    clHASH_init(&ctx);
    clHASH_update(&ctx, &is_nonleaf, 1);
    clHASH_update(&ctx, &block, VERITY_BLOCK_SIZE);
    const uint8_t* actual = clHASH_final(&ctx);
    return memcmp(actual, expected, VERITY_DIGEST_LEN) == 0;
}

static int verity_digester_thread(void* args) {
    verity_device_t* device = args;
    uint8_t digest[VERITY_DIGEST_LEN];
    while (true) {
        // Get the next read response to be digested.
        mtx_lock(&device->digester_mtx);
        do {
            if (list_is_empty(&device->to_digest)) {
                cnd_wait(&device->digester_cnd, &device->digester_mtx);
            }
            if (verity_get_mode(device) == VERITY_SHUTDOWN) {
                thrd_exit(EXIT_SUCCESS);
            }
        } while (list_is_empty(&device->to_digest));
        iotxn_t* txn = list_remove_head_type(&device->to_digest, iotxn_t, node);
        mtx_unlock(&device->digester_mtx);
        iotxn_t* prev = txn->context;
        txn->context = NULL;
        // Check for I/O error from previous read.
        lba_t off = txn->offset / VERITY_BLOCK_SIZE;
        if (txn->status != NO_ERROR) {
            xprintf("%s: error %d reading LBA %llu\n", device->dev.name, txn->status, off);
            verity_yield(device, prev);
            prev->ops->complete(prev, txn->status, 0);
            txn->ops->release(txn);
            continue;
        }
        //
        lba_t start = prev->offset / VERITY_BLOCK_SIZE;
        lba_t end = start + MIN(prev->length / VERITY_BLOCK_SIZE, VERITY_DIGESTS_PER_BLOCK - (start % VERITY_DIGESTS_PER_BLOCK));
        for (off = start; off < end; ++off) {
            if (verity_check_bit(device, off)) {
                continue;
            }
            txn->ops->copyfrom(txn, &digest, VERITY_DIGEST_LEN, (off % VERITY_DIGESTS_PER_BLOCK) * VERITY_DIGEST_LEN);
            if (!verity_match_digest(digest, prev, off - start, off < device->num_leaves)) {
                // TODO: verity failure?
                xprintf("%s: digest mismatch for LBA %llu\n", device->dev.name, off);
                verity_yield(device, prev);
                if (verity_get_mode(device) != VERITY_IGNORE) {
                    prev->ops->complete(prev, ERR_CHECKSUM_FAIL, 0);
                    break;
                }
            } else {
                verity_set_bit(device, prev->offset);
            }
        }
        txn->ops->release(txn);
        if (off < end) {
            continue;
        }
        // Re-clone the iotxn to insert back into the to_verify queue.
        mx_status_t status = prev->ops->clone(prev, &txn, 0);
        if (status != NO_ERROR) {
            xprintf("%s: error %d cloning iotxn\n", device->dev.name, status);
            verity_yield(device, prev);
            prev->ops->complete(prev, status, 0);
            continue;
        }
        txn->context = prev;
        mtx_lock(&device->verifier_mtx);
        list_add_tail(&device->to_verify, &txn->node);
        cnd_broadcast(&device->verifier_cnd);
        mtx_unlock(&device->verifier_mtx);
    }
    return NO_ERROR;
}

// Thread operations

static void verity_shutdown(verity_device_t* device) {
    verity_set_mode(device, VERITY_SHUTDOWN);
    iotxn_t* txn = NULL;
    iotxn_t* tmp = NULL;
    xprintf("%s: shutting down!\n", device->dev.name);
    // Clear device pointer from pending read iotxns.
    mtx_lock(&device->iotxns_mtx);
    list_for_every_entry_safe (&device->iotxns, txn, tmp, iotxn_t, node) {
        txn->context = NULL;
    }
    mtx_unlock(&device->iotxns_mtx);
    // Clear to_verify queue
    mtx_lock(&device->verifier_mtx);
    list_for_every_entry_safe (&device->to_verify, txn, tmp, iotxn_t, node) {
        iotxn_t* prev = txn->context;
        prev->ops->complete(prev, ERR_HANDLE_CLOSED, 0);
        txn->ops->release(txn);
    }
    cnd_broadcast(&device->verifier_cnd);
    mtx_unlock(&device->verifier_mtx);
    // Clear to_digest queue
    mtx_lock(&device->digester_mtx);
    list_for_every_entry_safe (&device->to_digest, txn, tmp, iotxn_t, node) {
        iotxn_t* prev = txn->context;
        prev->ops->complete(prev, ERR_HANDLE_CLOSED, 0);
        txn->ops->release(txn);
    }
    cnd_broadcast(&device->digester_cnd);
    mtx_unlock(&device->digester_mtx);
    // Threads have been signaled; clean them up.
    int result;
    for (size_t i = 0; i < device->num_threads; ++i) {
        thrd_join(device->threads[i], &result);
    }
    xprintf("%s: shutdown complete.\n", device->dev.name);
}

static mx_status_t verity_start(verity_device_t* device) {
    verity_set_mode(device, VERITY_ENFORCE);
    mx_status_t status = NO_ERROR;
    xprintf("%s: starting up.\n", device->dev.name);
    if (mtx_init(&device->iotxns_mtx, mtx_plain) != thrd_success ||
        mtx_init(&device->verifier_mtx, mtx_plain) != thrd_success ||
        cnd_init(&device->verifier_cnd) != thrd_success ||
        mtx_init(&device->digester_mtx, mtx_plain) != thrd_success ||
        cnd_init(&device->digester_cnd) != thrd_success) {
        status = ERR_NO_RESOURCES;
        xprintf("%s: error %d: failed to initialize thread variables\n", device->dev.name, status);
        return status;
    }
    list_initialize(&device->iotxns);
    list_initialize(&device->to_verify);
    list_initialize(&device->to_digest);
    // Start the threads.
    char name[20];
    for (int i = 0; status == NO_ERROR && i < VERITY_VERIFIER_THREADS; ++i) {
        snprintf(name, sizeof(name), "%s-verify:%u", device->dev.name, i);
        status = thrd_create_with_name(&device->threads[device->num_threads++], verity_verifier_thread, device, name);
    }
    for (int i = 0; status == NO_ERROR && i < VERITY_DIGESTER_THREADS; ++i) {
        snprintf(name, sizeof(name), "%s-digest:%u", device->dev.name, i);
        status = thrd_create_with_name(&device->threads[device->num_threads++], verity_digester_thread, device, name);
    }
    if (status != NO_ERROR) {
        xprintf("%s: error %d: failed to initialize threads\n", device->dev.name, status);
        verity_shutdown(device);
    } else {
        xprintf("%s: startup complete!\n", device->dev.name);
    }
    return status;
}

// Device protocol

static void verity_sync_read_cb(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

static ssize_t verity_set_root_digest(verity_device_t* device, const uint8_t* digest, size_t digest_len) {
    if (digest_len != VERITY_DIGEST_LEN) {
        xprintf("%s: wrong digest length (%zu for %d).\n", device->dev.name, digest_len, VERITY_DIGEST_LEN);
        return ERR_INVALID_ARGS;
    }
    // Find the last block
    lba_t start = 0;
    lba_t end = device->num_leaves;
    do {
        verity_get_level(device, end, &start, &end);
    } while ((end - start) > 1);
    // Synchronously read the last block
    iotxn_t* txn;
    mx_status_t status = iotxn_alloc(&txn, 0, VERITY_BLOCK_SIZE, 0);
    if (status != NO_ERROR) {
        xprintf("%s: error %d allocating iotxn\n", device->dev.name, status);
        return status;
    }
    completion_t completion = COMPLETION_INIT;
    txn->opcode = IOTXN_OP_READ;
    txn->offset = start * VERITY_BLOCK_SIZE;
    txn->length = VERITY_BLOCK_SIZE;
    txn->complete_cb = verity_sync_read_cb;
    txn->cookie = &completion;
    mx_device_t* parent = device->dev.parent;
    parent->ops->iotxn_queue(parent, txn);
    completion_wait(&completion, MX_TIME_INFINITE);
    if (!verity_match_digest(digest, txn, 0, false)) {
        // TODO: verity failure?
        xprintf("%s: root digest mismatch\n", device->dev.name);
        status = ERR_CHECKSUM_FAIL;
    } else {
        xprintf("%s: root digest set\n", device->dev.name);
        verity_set_bit(device, start);
        status = NO_ERROR;
    }
    txn->ops->release(txn);
    return status;
}

static mx_off_t verity_getsize(mx_device_t* dev) {
    verity_device_t* device = verity_get_device(dev);
    return device->num_leaves * VERITY_BLOCK_SIZE;
}

static ssize_t verity_ioctl(mx_device_t* dev, uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max) {
    verity_device_t* device = verity_get_device(dev);
    switch (op) {
    case IOCTL_BLOCK_VERITY_MODE: {
        if (cmd && cmdlen != sizeof(int)) {
            return ERR_INVALID_ARGS;
        }
        if (reply && max < sizeof(verity_mode_t)) {
            return ERR_NOT_ENOUGH_BUFFER;
        }
        if (cmd) {
            verity_set_mode(device, *((uint8_t*)cmd));
        }
        if (reply) {
            verity_mode_t *mode = reply;
            *mode = verity_get_mode(device);
        }
        return NO_ERROR;
    }
    case IOCTL_BLOCK_SET_VERITY_ROOT: {
        if (cmd && (cmdlen == 0 || (cmdlen != sig_len && cmdlen != VERITY_DIGEST_LEN))) {
            return ERR_INVALID_ARGS;
        }
        if (reply && max < sizeof(bool)) {
            return ERR_NOT_ENOUGH_BUFFER;
        }
        ssize_t rc = NO_ERROR;
        if (cmd) {
            rc = verity_set_root(device, cmd, cmdlen);
        }
        if (reply) {
            lba_range_t *top = &superblock.levels[levels_len -1];
            lba_t off = top->offset;
            bool *verified = reply;
                *verified = verity_check_all(device, &off, top->length);
        }
        return rc;
    }
    case IOCTL_BLOCK_GET_SIZE: {
        uint64_t* size = reply;
        if (max < sizeof(*size))
            return ERR_NOT_ENOUGH_BUFFER;
        *size = verity_getsize(dev);
        return sizeof(*size);
    }
    case IOCTL_BLOCK_GET_BLOCKSIZE: {
        uint64_t* blksize = reply;
        if (max < sizeof(*blksize))
            return ERR_NOT_ENOUGH_BUFFER;
        *blksize = VERITY_BLOCK_SIZE;
        return sizeof(*blksize);
    }
    default: {
        mx_device_t* parent = device->dev.parent;
        return parent->ops->ioctl(parent, op, cmd, cmdlen, reply, max);
    }
    }
}

static void verity_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    verity_device_t* device = verity_get_device(dev);
    if (device->mode == VERITY_BYPASS) {
        lba_t off = txn->offset / VERITY_BLOCK_SIZE;
        lba_t max = (txn->offset + txn->length) / VERITY_BLOCK_SIZE;
        if (txn->opcode == IOTXN_OP_WRITE) {
            verity_clear_all(device, off, max);
        }
        mx_device_t* parent = device->dev.parent;
        parent->ops->iotxn_queue(dev, txn);
        return;
    }
    // Sanity checks
    if (txn->opcode != IOTXN_OP_READ) {
        xprintf("%s: read-only device\n", device->dev.name);
        txn->ops->complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }
    if (txn->offset % VERITY_BLOCK_SIZE != 0) {
        xprintf("%s: offset is not block-aligned\n", device->dev.name);
        txn->ops->complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }
    if (txn->length % VERITY_BLOCK_SIZE != 0) {
        xprintf("%s: length is not block-aligned\n", device->dev.name);
        txn->ops->complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }
    if (txn->offset / VERITY_BLOCK_SIZE > device->num_leaves) {
        xprintf("%s: offset is out of bounds\n", device->dev.name);
        txn->ops->complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }
    // Constrain length to readable portion.
    txn->length = MIN((device->num_leaves * VERITY_BLOCK_SIZE) - txn->offset, txn->length);
    verity_queue_verified_read(device, txn);
}

static mx_status_t verity_release(mx_device_t* dev) {
    verity_device_t* device = verity_get_device(dev);
    verity_shutdown(device);
    mx_device_t* child = NULL;
    mx_device_t* temp = NULL;
    list_for_every_entry_safe (&dev->children, child, temp, mx_device_t, node) {
        device_remove(child);
    }
    free(device);
    return NO_ERROR;
}

static mx_protocol_device_t verity_proto = {
    .ioctl = verity_ioctl,
    .iotxn_queue = verity_iotxn_queue,
    .get_size = verity_getsize,
    .release = verity_release,
};

// Driver binding/unbinding

static mx_status_t verity_bind(mx_driver_t* drv, mx_device_t* dev) {
    mx_ssize_t rc = NO_ERROR;
    char name[20];
    snprintf(name, sizeof(name), "%s-verity", dev->name);
    // Check that logical block size is a multiple of the actual block size
    size_t blksize;
    rc = dev->ops->ioctl(dev, IOCTL_BLOCK_GET_BLOCKSIZE, NULL, 0, &blksize, sizeof(blksize));
    if (rc < 0) {
        xprintf("%s: error %zd when getting block size.\n", name, rc);
        return rc;
    }
    if (VERITY_BLOCK_SIZE % blksize != 0) {
        rc = ERR_NOT_SUPPORTED;
        xprintf("%s: error %zd: logical blksize %d not aligned with real blksize of %zu\n", name, rc, VERITY_BLOCK_SIZE, blksize);
        return rc;
    }
    // Determine how much of the device must be reserved for the hash tree.
    uint64_t size;
    rc = dev->ops->ioctl(dev, IOCTL_BLOCK_GET_SIZE, NULL, 0, &size, sizeof(size));
    if (rc < 0) {
        xprintf("%s: error %zd when getting device size\n", name, rc);
        return rc;
    }


    lba_t num_leaves = verity_get_max_leaves(size);
    if (num_leaves == 0) {
        rc = ERR_NOT_SUPPORTED;
        xprintf("%s: error %zd: device is too small: %llu\n", name, rc, size);
        return rc;
    }


    // Allocate the device and initialize its synchronization members.
    verity_device_t* device = calloc(1, sizeof(verity_device_t));
    if (!device) {
        rc = ERR_NO_MEMORY;
        xprintf("%s: error %zd: out of memory!\n", name, rc);
        return rc;
    }
    if (mtx_init(&device->bitmap_mtx, mtx_plain) != thrd_success ||
        mtx_init(&device->iotxns_mtx, mtx_plain) != thrd_success ||
        mtx_init(&device->verifier_mtx, mtx_plain) != thrd_success ||
        cnd_init(&device->verifier_cnd) != thrd_success ||
        mtx_init(&device->digester_mtx, mtx_plain) != thrd_success ||
        cnd_init(&device->digester_cnd) != thrd_success) {
        rc = ERR_NO_RESOURCES;
        xprintf("%s: error %zd: failed to initialize thread variables\n", name, rc);
        return rc;
    }
    list_initialize(&device->iotxns);
    list_initialize(&device->to_verify);
    list_initialize(&device->to_digest);
    // Set up the bitmap
    device->bitmap_len = ((size - 1) / 64) + 1;
    device->bitmap = calloc(device->bitmap_len, sizeof(uint64_t));
    if (!device->bitmap) {
        rc = ERR_NO_MEMORY;
        xprintf("%s: error %zd: out of memory!\n", name, rc);
        return rc;
    }
    // Start the threads.
    mx_status_t status = NO_ERROR;
    for (int i = 0; status == NO_ERROR && i < VERITY_VERIFIER_THREADS; ++i) {
        snprintf(name, sizeof(name), "%s-verify:%u", device->dev.name, i);
        status = thrd_create_with_name(&device->threads[device->num_threads++], verity_verifier_thread, device, name);
    }
    for (int i = 0; status == NO_ERROR && i < VERITY_DIGESTER_THREADS; ++i) {
        snprintf(name, sizeof(name), "%s-digest:%u", device->dev.name, i);
        status = thrd_create_with_name(&device->threads[device->num_threads++], verity_digester_thread, device, name);
    }
    if (status != NO_ERROR) {
        rc = ERR_NO_RESOURCES;
        xprintf("%s: error %zd: failed to initialize threads\n", device->dev.name, rc);
        return status;
    }
    snprintf(name, sizeof(name), "%s-verity", device->dev.name);
    device_init(&device->dev, drv, name, &verity_proto);
    device->dev.protocol_id = MX_PROTOCOL_BLOCK;
    device_add(&device->dev, dev);
    return status;
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
