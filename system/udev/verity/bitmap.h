// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>

#include <magenta/types.h>

typedef struct bitmap bitmap_t;

bitmap_t* bitmap_init(uint64_t len);
uint64_t bitmap_len(const bitmap_t* bitmap);
bool bitmap_check_one(bitmap_t* bitmap, uint64_t offset);
bool bitmap_check_all(bitmap_t* bitmap, uint64_t* off, uint64_t max);
void bitmap_set_one(bitmap_t* bitmap, uint64_t off);
void bitmap_clear_all(bitmap_t* bitmap, uint64_t off, uint64_t max);
void bitmap_free(bitmap_t* bitmap);
