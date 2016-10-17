// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <assert.h>

#include <ddk/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// MXDM_BLOCK_SIZE is the size of a block of data.  The actual block device's
// block size must divide this number evenly.
#define MXDM_BLOCK_SIZE 8192

#define MXDM_VERITY_MAGIC 0x797469726556784Dull // 'MxVerity'
#define MXDM_VERITY_DIGEST_LEN 32 // SHA-256
#define MXDM_VERITY_MAX_DEPTH 8
#define MXDM_VERITY_MAX_SALT 256
#define MXDM_VERITY_MAX_KEY_LEN 256
#define MXDM_VERITY_VERSION_1_0 0x00010000

static_assert(MXDM_BLOCK_SIZE / MXDM_VERITY_DIGEST_LEN >=
                  (1 << ((sizeof(uint64_t) * 8) / MXDM_VERITY_MAX_DEPTH)),
              "Hash tree must be deep enough to address all blocks");

#define IOCTL_MXDM_VERITY_GET_MODE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_MXDM, 1)
#define IOCTL_MXDM_VERITY_SET_MODE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_MXDM, 2)
#define IOCTL_MXDM_VERITY_SET_ROOT \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_MXDM, 3)

typedef struct verity_header {
  // Magic 
  uint64_t magic;
  uint32_t version;
  uint8_t digest[MXDM_VERITY_DIGEST_LEN];
  uint8_t uuid[16];
  // Hash tree structure.
  uint64_t begins[MXDM_VERITY_MAX_DEPTH];
  uint64_t ends[MXDM_VERITY_MAX_DEPTH];
  uint8_t depth;
  // See https://bugs.chromium.org/p/chromium/issues/detail?id=194620
  uint8_t salt[MXDM_VERITY_MAX_SALT];
  uint16_t salt_len;
  // cryptolib currently only supports 2048-bit RSA with SHA-256 and PKCS 1.5.
  uint8_t signature[MXDM_VERITY_MAX_KEY_LEN];
  uint16_t signature_len;
  uint8_t key_digest[MXDM_VERITY_DIGEST_LEN];
}
verity_header_t;
static_assert(sizeof(verity_header_t) <= MXDM_BLOCK_SIZE, "Verity header must fit in a single block");

typedef enum verity_mode {
  kVerityModeIgnore,
  kVerityModeWarn,
  kVerityModeEnforce,
} verity_mode_t;


#ifdef __cplusplus
}
#endif  // __cplusplus
