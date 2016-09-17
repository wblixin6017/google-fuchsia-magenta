// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bitmap.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <threads.h>

#include <magenta/types.h>

struct bitmap {
    uint64_t* data;
    uint64_t len;
    mtx_t mtx;
};

bitmap_t* bitmap_init(uint64_t max) {
    if (max == 0) {
        return NULL;
    }
    bitmap_t* bitmap = calloc(1, sizeof(bitmap_t));
    if (!bitmap) {
        return NULL;
    }
    bitmap->len = ((max - 1) / 64) + 1;
    bitmap->data = calloc(bitmap->len, sizeof(uint64_t));
    if (!bitmap->data) {
        bitmap_free(bitmap);
        return NULL;
    }
    if (mtx_init(&bitmap->mtx, mtx_plain) != thrd_success) {
        bitmap_free(bitmap);
        return NULL;
    }
    return bitmap;
}

uint64_t bitmap_len(const bitmap_t *bitmap) {
    return bitmap->len;
}

bool bitmap_check_one(bitmap_t* bitmap, uint64_t offset) {
    bool is_set = false;
    mtx_lock(&bitmap->mtx);
    is_set = (bitmap->data[offset / 64] & 1ULL << (63 - (offset % 64)));
    mtx_unlock(&bitmap->mtx);
    return is_set;
}

bool bitmap_check_all(bitmap_t* bitmap, uint64_t* off, uint64_t max) {
    uint64_t i = *off / 64;
    uint64_t n = max / 64;
    if (n > bitmap->len) {
        n = bitmap->len;
    }
    mtx_lock(&bitmap->mtx);
    uint64_t count = __builtin_clz(~bitmap->data[i++] << (*off % 64));
    while (count == 64 && i <= n) {
        count = __builtin_clz(~bitmap->data[i++]);
    }
    mtx_unlock(&bitmap->mtx);
    *off = (((i - 1) * 64) - *off);
    if (*off > max) {
        *off = max;
    }
    return *off == max;
}

void bitmap_set_one(bitmap_t* bitmap, uint64_t off) {
    mtx_lock(&bitmap->mtx);
    bitmap->data[off / 64] |= 1ULL << (63 - (off % 64));
    mtx_unlock(&bitmap->mtx);
}

void bitmap_clear_all(bitmap_t* bitmap, uint64_t off, uint64_t max) {
    uint64_t i = off / 64;
    uint64_t n = max / 64;
    if (n > bitmap->len) {
        n = bitmap->len;
    }
    mtx_lock(&bitmap->mtx);
    if (off % 64 != 0) {
        bitmap->data[i++] &= (~0ULL) << (off % 64);
    }
    while (i < max / 64) {
        bitmap->data[i++] = 0;
    }
    if (max % 64 != 0) {
        bitmap->data[i] &= (~0ULL) >> (max % 64);
    }
    mtx_unlock(&bitmap->mtx);
}

void bitmap_free(bitmap_t* bitmap) {
    if (!bitmap) {
        return;
    }
    if (bitmap->data) {
        free(bitmap->data);
    }
    free(bitmap);
}
