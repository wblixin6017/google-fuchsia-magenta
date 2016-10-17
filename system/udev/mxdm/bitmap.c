// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides a compressible bitmap that MXDM uses to mark and clear
// blocks.
//
// A bitmap made of 'chunks', each of which can either be a simple, uncompressed
// bitmap (raw) or a compressed, run-length encoding (RLE).  Initially, all
// chunks are RLE chunks. An RLE chunks is converted it would use more memory
// than a raw chunk.  The function mxdm_bitmap_compress can be used to convert
// raw chunks back to RLEs if doing so would use less memory.

#define MXDM_IMPLEMENTATION

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include <magenta/fuchsia-types.h>
#include <magenta/listnode.h>
#include <magenta/types.h>

#include "bitmap.h"
#include "common.h"
#include "mxdm.h"

// Constants

// The number of bits in a bitmap chunk.
#define MXDM_BITS_PER_CHUNK (MXDM_BLOCK_SIZE * 8)

// Types

// A run-length encoding (RLE) of a sequence of bits set to 1.
typedef struct mxdm_bitmap_rle_elem {
  // Handle used to link this element to a RLE chunk.
  list_node_t node;
  // The start of this run of 1-bits.
  uint64_t bitoff;
  // The number of 1-bits in this run.
  uint64_t bitlen;
} mxdm_bitmap_rle_elem_t;

// A compressed chunk of a bitmap made up of a list of RLEs.
typedef struct mxdm_bitmap_rle {
  // The number of elements in 'elems'.
  size_t length;
  // A list of mxdm_bitmap_rle_elem_t.
  list_node_t elems;
} mxdm_bitmap_rle_t;

// A chunked, hybrid bitmap.
typedef struct mxdm_bitmap {
  // The numbers of bits in this map.
  uint64_t bitlen;
  // The numbers of chunks of bits in this map, which is the length of 'data'.
  uint64_t chunks;
  // An array of chunks of bitmap data.  Each is either an uncompressed array of
  // bits or a run-length encoding list.
  union {
    uint64_t *raw;
    mxdm_bitmap_rle_t *rle;
  } * data;
  // A smaller bitmap indicating which chunks are RLE chunks.  This field is
  // NULL when 'bitlen' is less than MXDM_BITS_PER_CHUNK, and the sole chunk
  // in 'data' is always raw.
  struct mxdm_bitmap *use_rle;
} mxdm_bitmap_t;

// Forward declarations

// Returns true if a single bit in the 'bitmap' given by 'bitoff' is set.
static bool mxdm_bitmap_one(mxdm_bitmap_t *bitmap, uint64_t bitoff);

// Releases memory associated with a specific RLE chunk.
static void mxdm_bitmap_rle_free(mxdm_bitmap_rle_t *rle);

// Adds a new RLE element to the RLE chunk.  This can fail if the RLE chunk has
// gotten too large, or is OOM.
static mx_status_t mxdm_bitmap_rle_add(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff, uint64_t bitlen,
                                       mxdm_bitmap_rle_elem_t **out);

// Acts the same as mxdm_bitmap_get, but for a specific RLE chunk.
static bool mxdm_bitmap_rle_get(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                uint64_t *bitoff, uint64_t bitmax);

// Acts the same as mxdm_bitmap_set, but for a specific RLE chunk.  This can
// fail if the RLE chunk has gotten too large, or is OOM.
static mx_status_t mxdm_bitmap_rle_set(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff);

// Acts the same as mxdm_bitmap_clr, but for a specific RLE chunk.  This can
// fail if the RLE chunk has gotten too large, or is OOM.
static mx_status_t mxdm_bitmap_rle_clr(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff, uint64_t bitmax);

// Convets a specific RLE chunk into an raw chunk.
static mx_status_t mxdm_bitmap_rle_to_raw(mxdm_bitmap_t *bitmap,
                                          uint64_t bitoff);

// Acts the same as mxdm_bitmap_get, but for a specific raw chunk.
static bool mxdm_bitmap_raw_get(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                uint64_t *bitoff, uint64_t bitmax);

// Acts the same as mxdm_bitmap_set, but for a specific raw chunk.
static mx_status_t mxdm_bitmap_raw_set(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff);

// Acts the same as mxdm_bitmap_clr, but for a specific raw chunk.
static mx_status_t mxdm_bitmap_raw_clr(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff, uint64_t bitmax);

// Returns true if the raw chunk at 'bitoff' would use less memory as an RLE
// chunk.
static bool mxdm_bitmap_raw_is_compressible(mxdm_bitmap_t *bitmap,
                                            uint64_t bitoff);

// Convets a specific raw chunk into an RLE chunk.
static mx_status_t mxdm_bitmap_raw_to_rle(mxdm_bitmap_t *bitmap,
                                          uint64_t bitoff);

// Protected functions

mx_status_t mxdm_bitmap_init(uint64_t bitlen, mxdm_bitmap_t **out) {
  assert(bitlen != 0);
  assert(out);
  mx_status_t rc = NO_ERROR;
  mxdm_bitmap_t *bitmap = calloc(1, sizeof(mxdm_bitmap_t));
  if (!bitmap) {
    MXDM_TRACE("out of memory!");
    return ERR_NO_MEMORY;
  }
  bitmap->bitlen = bitlen;
  bitmap->chunks = ((bitlen - 1) / MXDM_BITS_PER_CHUNK) + 1;
  bitmap->data = calloc(bitmap->chunks, sizeof(void *));
  if (!bitmap->data) {
    MXDM_TRACE("out of memory!");
    rc = ERR_NO_MEMORY;
    goto fail;
  }
  // Handle devices with only a few blocks.
  if (bitmap->chunks == 1) {
    bitmap->data[0].raw = calloc(MXDM_BITS_PER_CHUNK / 64, sizeof(uint64_t));
    if (!bitmap->data[0].raw) {
      MXDM_TRACE("out of memory!");
      rc = ERR_NO_MEMORY;
      goto fail;
    }
    goto done;
  }
  // Handle larger devices.
  rc = mxdm_bitmap_init(bitmap->chunks, &bitmap->use_rle);
  if (rc < 0) {
    goto fail;
  }
  for (size_t i = 0; i < bitmap->chunks; ++i) {
    mxdm_bitmap_rle_t *rle = calloc(1, sizeof(mxdm_bitmap_rle_t));
    if (!rle) {
      MXDM_TRACE("out of memory!");
      rc = ERR_NO_MEMORY;
      goto fail;
    }
    list_initialize(&rle->elems);
    rle->length = 0;
    rc = mxdm_bitmap_set(bitmap->use_rle, i);
    if (rc < 0) {
      goto fail;
    }
    bitmap->data[i].rle = rle;
  }
done:
  *out = bitmap;
  return NO_ERROR;
fail:
  mxdm_bitmap_free(bitmap);
  return rc;
}

void mxdm_bitmap_free(mxdm_bitmap_t *bitmap) {
  if (!bitmap) {
    return;
  }
  MXDM_TRACE("freeing bitmap %p of length %lu", bitmap, bitmap->bitlen);
  uint64_t n = ((bitmap->bitlen - 1) / MXDM_BITS_PER_CHUNK) + 1;
  if (bitmap->data) {
    for (uint64_t i = 0; i < n;) {
      if (mxdm_bitmap_one(bitmap->use_rle, i)) {
        mxdm_bitmap_rle_free(bitmap->data[i].rle);
        bitmap->data[i].rle = NULL;
      } else {
        if (bitmap->data[i].raw) {
          free(bitmap->data[i].raw);
        }
        bitmap->data[i].raw = NULL;
      }
    }
    free(bitmap->data);
    bitmap->data = NULL;
  }
  mxdm_bitmap_free(bitmap->use_rle);
  bitmap->use_rle = NULL;
  free(bitmap);
}

bool mxdm_bitmap_get(mxdm_bitmap_t *bitmap, uint64_t *bitoff, uint64_t bitmax) {
  assert(bitoff);
  // It's not an error to have a NULL bitmap here; use_rle often is.
  if (!bitmap || *bitoff >= bitmap->bitlen) {
    return false;
  }
  if (*bitoff >= bitmax) {
    return true;
  }
  uint64_t i = *bitoff / MXDM_BITS_PER_CHUNK;
  uint64_t n = MIN(((bitmax - 1) / MXDM_BITS_PER_CHUNK) + 1, bitmap->chunks);
  uint64_t off = *bitoff % MXDM_BITS_PER_CHUNK;
  uint64_t max = MXDM_BITS_PER_CHUNK;
  bool result = true;
  for (; i < n && result; ++i) {
    if (i == n - 1) {
      max = ((bitmax - 1) % MXDM_BITS_PER_CHUNK) + 1;
    }
    *bitoff -= off;
    if (mxdm_bitmap_one(bitmap->use_rle, i)) {
      result = mxdm_bitmap_rle_get(bitmap, i, &off, max);
    } else {
      result = mxdm_bitmap_raw_get(bitmap, i, &off, max);
    }
    *bitoff += off;
    off = 0;
  }
  return (*bitoff == bitmax);
}

mx_status_t mxdm_bitmap_set(mxdm_bitmap_t *bitmap, uint64_t bitoff) {
  assert(bitmap);
  if (bitoff >= bitmap->bitlen) {
    MXDM_TRACE("out of range: %lu", bitoff);
    return ERR_INVALID_ARGS;
  }
  uint64_t chunk = bitoff / MXDM_BITS_PER_CHUNK;
  bitoff %= MXDM_BITS_PER_CHUNK;
  mx_status_t rc = NO_ERROR;
  if (mxdm_bitmap_one(bitmap->use_rle, chunk)) {
    rc = mxdm_bitmap_rle_set(bitmap, chunk, bitoff);
    if (rc != ERR_OUT_OF_RANGE) {
      return rc;
    }
    rc = mxdm_bitmap_rle_to_raw(bitmap, chunk);
  }
  if (rc == NO_ERROR) {
    rc = mxdm_bitmap_raw_set(bitmap, chunk, bitoff);
  }
  return rc;
}

mx_status_t mxdm_bitmap_clr(mxdm_bitmap_t *bitmap, uint64_t bitoff,
                            uint64_t bitmax) {
  assert(bitmap);
  mx_status_t rc = NO_ERROR;
  if (bitoff >= bitmax) {
    return NO_ERROR;
  }
  uint64_t i = bitoff / MXDM_BITS_PER_CHUNK;
  uint64_t n = MIN(((bitmax - 1) / MXDM_BITS_PER_CHUNK) + 1, bitmap->chunks);
  uint64_t off = bitoff % MXDM_BITS_PER_CHUNK;
  uint64_t max = MXDM_BITS_PER_CHUNK;
  for (; i < n && rc == NO_ERROR; ++i) {
    if (i == n - 1) {
      max = ((bitmax - 1) % MXDM_BITS_PER_CHUNK) + 1;
    }
    if (mxdm_bitmap_one(bitmap->use_rle, i)) {
      rc = mxdm_bitmap_rle_clr(bitmap, i, off, max);
      if (rc != ERR_OUT_OF_RANGE) {
        continue;
      }
      rc = mxdm_bitmap_rle_to_raw(bitmap, i);
    }
    if (rc == NO_ERROR) {
      rc = mxdm_bitmap_raw_clr(bitmap, i, off, max);
    }
    off = 0;
  }
  return rc;
}

// TODO(aarongreen): Is this being tested?
void mxdm_bitmap_compress(mxdm_bitmap_t *bitmap) {
  assert(bitmap);
  if (!bitmap->use_rle) {
    return;
  }
  mxdm_bitmap_compress(bitmap->use_rle);
  for (uint64_t i = 0; i < bitmap->chunks; ++i) {
    if (mxdm_bitmap_raw_is_compressible(bitmap, i)) {
      mxdm_bitmap_raw_to_rle(bitmap, i);
    }
  }
}

// Private functions

static inline void mxdm_bitmap_assert(const mxdm_bitmap_t *bitmap,
                                      uint64_t chunk, const uint64_t *bitoff,
                                      uint64_t bitmax) {
  assert(bitmap);
  assert(bitmax == 0 || chunk < bitmap->chunks);
  assert(bitoff);
  assert(*bitoff < MXDM_BITS_PER_CHUNK);
  assert(bitmax <= MXDM_BITS_PER_CHUNK);
}

static void mxdm_bitmap_rle_free(mxdm_bitmap_rle_t *rle) {
  if (!rle) {
    return;
  }
  mxdm_bitmap_rle_elem_t *elem = NULL;
  mxdm_bitmap_rle_elem_t *temp = NULL;
  list_for_every_entry_safe(&rle->elems, elem, temp, mxdm_bitmap_rle_elem_t,
                            node) {
    list_delete(&elem->node);
    --rle->length;
    free(elem);
  }
  assert(rle->length == 0);
  free(rle);
}

static mx_status_t mxdm_bitmap_rle_add(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff, uint64_t bitlen,
                                       mxdm_bitmap_rle_elem_t **out) {
  mxdm_bitmap_assert(bitmap, chunk, &bitoff, bitoff + bitlen);
  mxdm_bitmap_rle_t *rle = bitmap->data[chunk].rle;
  mxdm_bitmap_rle_elem_t *elem = NULL;
  if (rle->length >= MXDM_BLOCK_SIZE / sizeof(mxdm_bitmap_rle_elem_t)) {
    return ERR_OUT_OF_RANGE;
  }
  // TODO(aarongreen): I anticipate a lot of malloc overhead from lots of
  // small allocations; we should probably allocate a pool of elems and double
  // as needed to amortize overhead.
  elem = calloc(1, sizeof(mxdm_bitmap_rle_elem_t));
  if (!elem) {
    // Retry once.
    mxdm_bitmap_compress(bitmap);
    elem = calloc(1, sizeof(mxdm_bitmap_rle_elem_t));
  }
  if (!elem) {
    MXDM_TRACE("out of memory!");
    return ERR_NO_MEMORY;
  }
  elem->bitoff = bitoff;
  elem->bitlen = bitlen;
  *out = elem;
  return NO_ERROR;
}

static bool mxdm_bitmap_one(mxdm_bitmap_t *bitmap, uint64_t bitoff) {
  // Skip assertions; bitmap == NULL is valid.
  return mxdm_bitmap_get(bitmap, &bitoff, bitoff + 1);
}

static bool mxdm_bitmap_rle_get(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                uint64_t *bitoff, uint64_t bitmax) {
  mxdm_bitmap_assert(bitmap, chunk, bitoff, bitmax);
  const mxdm_bitmap_rle_t *rle = bitmap->data[chunk].rle;
  mxdm_bitmap_rle_elem_t *elem;
  list_for_every_entry(&rle->elems, elem, mxdm_bitmap_rle_elem_t, node) {
    if (*bitoff < elem->bitoff) {
      break;
    }
    if (*bitoff < elem->bitoff + elem->bitlen) {
      *bitoff = elem->bitoff + elem->bitlen;
      break;
    }
  }
  if (*bitoff > bitmax) {
    *bitoff = bitmax;
  }
  return *bitoff == bitmax;
}

static mx_status_t mxdm_bitmap_rle_set(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff) {
  mxdm_bitmap_assert(bitmap, chunk, &bitoff, MXDM_BITS_PER_CHUNK);
  mx_status_t rc = NO_ERROR;
  mxdm_bitmap_rle_t *rle = bitmap->data[chunk].rle;
  mxdm_bitmap_rle_elem_t *elem = NULL;
  // Special case: empty list
  if (list_is_empty(&rle->elems)) {
    rc = mxdm_bitmap_rle_add(bitmap, chunk, bitoff, 1, &elem);
    if (rc < 0) {
      return rc;
    }
    list_add_tail(&rle->elems, &elem->node);
    ++rle->length;
    return NO_ERROR;
  }
  // General case: We iterate until we find an element that starts after bitoff,
  // or we run out of elements.
  mxdm_bitmap_rle_elem_t *prev = NULL;
  list_for_every_entry(&rle->elems, elem, mxdm_bitmap_rle_elem_t, node) {
    // Bit is right before elem
    if (bitoff + 1 == elem->bitoff) {
      if (prev) {
        // prev and elem can be merged.
        prev->bitlen += elem->bitlen;
        list_delete(&elem->node);
        --rle->length;
        free(elem);
      } else {
        // Extend elem
        --elem->bitoff;
        ++elem->bitlen;
      }
      return NO_ERROR;
    }
    // There's a gap between the bit and the next element.
    if (bitoff < elem->bitoff) {
      rc = mxdm_bitmap_rle_add(bitmap, chunk, bitoff, 1, &prev);
      if (rc < 0) {
        return rc;
      }
      list_add_before(&elem->node, &prev->node);
      ++rle->length;
      return NO_ERROR;
    }
    // Bit is already in an element.
    if (bitoff < elem->bitoff + elem->bitlen) {
      return NO_ERROR;
    }
    // Bit is right at the end of an element.
    if (bitoff == elem->bitoff + elem->bitlen) {
      ++elem->bitlen;
      // We might merge with the next RLE. Save and keep going.
      prev = elem;
    }
  }
  // We ran out of elements.  Add one at the end.
  rc = mxdm_bitmap_rle_add(bitmap, chunk, bitoff, 1, &elem);
  if (rc < 0) {
    return rc;
  }
  list_add_tail(&rle->elems, &elem->node);
  ++rle->length;
  return NO_ERROR;
}

static mx_status_t mxdm_bitmap_rle_clr(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff, uint64_t bitmax) {
  mxdm_bitmap_assert(bitmap, chunk, &bitoff, bitmax);
  mx_status_t rc = NO_ERROR;
  mxdm_bitmap_rle_t *rle = bitmap->data[chunk].rle;
  mxdm_bitmap_rle_elem_t *elem = NULL;
  mxdm_bitmap_rle_elem_t *temp = NULL;
  list_for_every_entry_safe(&rle->elems, elem, temp, mxdm_bitmap_rle_elem_t,
                            node) {
    if (elem->bitoff + elem->bitlen < bitoff) {
      continue;
    }
    if (bitmax < elem->bitoff) {
      break;
    }
    if (elem->bitoff < bitoff) {
      if (elem->bitoff + elem->bitlen < bitmax) {
        // 'elem' contains 'bitoff'.
        elem->bitlen = bitoff - elem->bitoff;
        break;
      } else {
        // 'elem' contains [bitoff, bitmax).
        elem->bitlen = bitoff - elem->bitoff;
        rc = mxdm_bitmap_rle_add(bitmap, chunk, bitmax,
                                 elem->bitoff + elem->bitlen - bitmax, &temp);
        if (rc < 0) {
          return rc;
        }
        list_add_after(&elem->node, &temp->node);
        ++rle->length;
        break;
      }
    } else {
      if (bitmax < elem->bitoff + elem->bitlen) {
        // 'elem' contains 'bitmax'
        elem->bitoff = bitmax;
        elem->bitlen = elem->bitoff + elem->bitlen - bitmax;
        break;
      } else {
        // [bitoff, bitmax) fully contains 'elem'.
        list_delete(&elem->node);
        free(elem);
        --rle->length;
      }
    }
  }
  return NO_ERROR;
}

static mx_status_t mxdm_bitmap_rle_to_raw(mxdm_bitmap_t *bitmap,
                                          uint64_t chunk) {
  assert(bitmap);
  assert(chunk < bitmap->chunks);
  mx_status_t rc = NO_ERROR;
  mxdm_bitmap_rle_t *rle = bitmap->data[chunk].rle;
  uint64_t *raw = calloc(MXDM_BITS_PER_CHUNK / 64, sizeof(uint64_t));
  if (!raw) {
    // Retry once
    mxdm_bitmap_compress(bitmap);
    raw = calloc(MXDM_BITS_PER_CHUNK / 64, sizeof(uint64_t));
  }
  if (!raw) {
    MXDM_TRACE("out of memory!");
    rc = ERR_NO_MEMORY;
    goto fail;
  }
  // Record that we're using a raw chunk now.
  rc = mxdm_bitmap_clr(bitmap->use_rle, chunk, chunk + 1);
  if (rc < 0) {
    goto fail;
  }
  bitmap->data[chunk].raw = raw;
  // It's easier to set all true and poke holes with existing functions.
  memset(raw, 0xFF, MXDM_BITS_PER_CHUNK / 8);
  uint64_t bitoff = 0;
  mxdm_bitmap_rle_elem_t *elem = NULL;
  list_for_every_entry(&rle->elems, elem, mxdm_bitmap_rle_elem_t, node) {
    rc = mxdm_bitmap_raw_clr(bitmap, chunk, bitoff, elem->bitoff);
    if (rc < 0) {
      goto fail;
    }
    bitoff = elem->bitoff + elem->bitlen;
  }
  rc = mxdm_bitmap_raw_clr(bitmap, chunk, bitoff, MXDM_BITS_PER_CHUNK);
  if (rc < 0) {
    goto fail;
  }
  //
  mxdm_bitmap_rle_free(rle);
  return NO_ERROR;
fail:
  // Restore the RLE chunk and clean up.
  mxdm_bitmap_set(bitmap->use_rle, chunk);
  bitmap->data[chunk].rle = rle;
  if (raw) {
    free(raw);
  }
  return rc;
}

static_assert(UINT64_MAX == ULLONG_MAX, "uint64_t is not unsigned long long");
#define CLZ64(u64) (u64 == 0 ? 64 : __builtin_clzll(u64))
static bool mxdm_bitmap_raw_get(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                uint64_t *bitoff, uint64_t bitmax) {
  mxdm_bitmap_assert(bitmap, chunk, bitoff, bitmax);
  bool compress_on_success = (*bitoff == 0 && bitmax == MXDM_BITS_PER_CHUNK);
  const uint64_t *raw = bitmap->data[chunk].raw;
  uint64_t i = *bitoff / 64;
  uint64_t n = ((bitmax - 1) / 64) + 1;
  uint64_t val = ~raw[i] << (*bitoff % 64);
  if (val != 0) {
    *bitoff = MIN(*bitoff + CLZ64(val), bitmax);
    return *bitoff == bitmax;
  }
  for (++i; i < n; ++i) {
    if (~raw[i] != 0) {
      *bitoff = ((i - 1) * 64) + CLZ64(~raw[i]);
      break;
    }
  }
  if (i < n && *bitoff < bitmax) {
    return false;
  }
  *bitoff = bitmax;
  // The whole block is set, compress it.
  if (compress_on_success) {
    mxdm_bitmap_raw_to_rle(bitmap, chunk);
  }
  return true;
}
#undef CLZ64

static mx_status_t mxdm_bitmap_raw_set(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff) {
  mxdm_bitmap_assert(bitmap, chunk, &bitoff, MXDM_BITS_PER_CHUNK);
  uint64_t *raw = bitmap->data[chunk].raw;
  raw[bitoff >> 6] |= 0x8000000000000000ULL >> (bitoff & 63);
  return NO_ERROR;
}

// TODO(aarongreen): When [bitoff,bitmax) == [0,64k), it would be quicker to
// just replace the chunk with an RLE.
static mx_status_t mxdm_bitmap_raw_clr(mxdm_bitmap_t *bitmap, uint64_t chunk,
                                       uint64_t bitoff, uint64_t bitmax) {
  mxdm_bitmap_assert(bitmap, chunk, &bitoff, bitmax);
  uint64_t *raw = bitmap->data[chunk].raw;
  uint64_t i = bitoff / 64;
  uint64_t n = bitmax / 64;
  if (i == n && (bitmax % 64) != 0) {
    raw[i] &= ~((~0ULL) >> (bitoff % 64) & (~0ULL) << (64 - (bitmax % 64)));
    return NO_ERROR;
  }
  if (bitoff % 64 != 0) {
    raw[i++] &= (~0ULL) << (64 - bitoff % 64);
  }
  while (i < n) {
    raw[i++] = 0;
  }
  if (bitmax % 64 != 0) {
    raw[i] &= (~0ULL) >> (bitmax % 64);
  }
  // The whole block is clear, compress it.
  if (bitoff == 0 && bitmax == MXDM_BITS_PER_CHUNK) {
    mxdm_bitmap_raw_to_rle(bitmap, chunk);
  }
  return NO_ERROR;
}

static bool mxdm_bitmap_raw_is_compressible(mxdm_bitmap_t *bitmap,
                                            uint64_t chunk) {
  assert(bitmap);
  assert(chunk < bitmap->chunks);
  uint64_t *raw = bitmap->data[chunk].raw;
  uint8_t bitseq = 0;
  size_t num_elems = 0;
  size_t max_elems = (MXDM_BITS_PER_CHUNK / 8) / sizeof(mxdm_bitmap_rle_elem_t);
  for (uint64_t bitoff = 0; bitoff < MXDM_BITS_PER_CHUNK; ++bitoff) {
    // Shift previous bit up and add next from bitmap.  Works across
    // uint64_t boundaries.
    bitseq = ((bitseq & 1) << 1) | ((raw[bitoff / 64] >> (bitoff & 63)) & 1);
    // Only a bit sequence of "01" requires a new element.
    if (bitseq != 1) {
      continue;
    }
    ++num_elems;
    if (num_elems >= max_elems) {
      return false;
    }
  }
  return true;
}

static mx_status_t mxdm_bitmap_raw_to_rle(mxdm_bitmap_t *bitmap,
                                          uint64_t chunk) {
  assert(bitmap);
  assert(chunk < bitmap->chunks);
  mx_status_t rc = NO_ERROR;
  // Allocate the RLE chunk
  mxdm_bitmap_rle_t *rle = calloc(1, sizeof(mxdm_bitmap_rle_t));
  if (!rle) {
    MXDM_TRACE("out of memory!");
    rc = ERR_NO_MEMORY;
    goto fail;
  }
  // Initialize the RLE chunk
  list_initialize(&rle->elems);
  rle->length = 0;
  rc = mxdm_bitmap_set(bitmap->use_rle, chunk);
  if (rc < 0) {
    goto fail;
  }
  // Add the values to the RLE chunk from the raw chunk
  uint64_t *raw = bitmap->data[chunk].raw;
  mxdm_bitmap_rle_elem_t *elem = NULL;
  for (uint64_t bitoff = 0; bitoff < MXDM_BITS_PER_CHUNK; ++bitoff) {
    uint64_t bitend = bitoff;
    mxdm_bitmap_raw_get(bitmap, chunk, &bitend, MXDM_BITS_PER_CHUNK);
    if (bitoff == bitend) {
      // First bit was unset; move forward.
      continue;
    }
    rc = mxdm_bitmap_rle_add(bitmap, chunk, bitoff, bitend - bitoff, &elem);
    if (rc < 0) {
      goto fail;
    }
    list_add_tail(&rle->elems, &elem->node);
    ++rle->length;
  }
  rc = mxdm_bitmap_set(bitmap->use_rle, chunk);
  if (rc < 0) {
    goto fail;
  }
  // Okay, make the changeover in the bitmap
  bitmap->data[chunk].rle = rle;
  free(raw);
  return NO_ERROR;
fail:
  if (rle) {
    mxdm_bitmap_rle_free(rle);
  }
  return rc;
}

#undef MXDM_IMPLEMENTATION
