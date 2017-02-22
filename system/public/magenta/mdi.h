// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <assert.h>
#include <stdint.h>

__BEGIN_CDECLS;

// MDI Nodes are aligned to 8 byte boundaries
#define MDI_ALIGNMENT   8
#define MDI_ALIGN(x)    (((x) + MDI_ALIGNMENT - 1) & ~(MDI_ALIGNMENT - 1))

// MDI node type
typedef enum {
    MDI_UINT8,      // unsigned 8-bit integer type
    MDI_INT32,      // signed 32-bit integer type
    MDI_UINT32,     // unsigned 32-bit integer type
    MDI_UINT64,     // unsigned 64-bit integer type
    MDI_BOOLEAN,    // boolean type
    MDI_STRING,     // zero terminated char string
    MDI_LIST,       // node is a list of children
    MDI_INVALID_TYPE = 31,
} mdi_type_t;

// MDI node identifier
// encodes both name and type of the node
typedef uint32_t mdi_id_t;
#define MDI_TYPE_SHIFT          24
#define MDI_TYPE_MASK           0xFF

#define MDI_MAX_ID              ((1 << MDI_TYPE_SHIFT) - 1)
#define MDI_ID_TYPE(id)         (mdi_type_t)(((id) >> MDI_TYPE_SHIFT) & MDI_TYPE_MASK)
#define MDI_ID_NUM(id)          ((id) & MDI_MAX_ID)
#define MDI_MAKE_ID(type, num)  ((type << MDI_TYPE_SHIFT) | num)

#define MDI_MAGIC                   0x2149444D  // "MDI!"

static const mdi_id_t mdi_root_id = MDI_MAKE_ID(MDI_LIST, 0);

// mdi_node_t represents a node in the device index.
// For integer and boolean types, the mdi_node_t is self contained and
// mdi_node_t.length is sizeof(mdi_node_t).
// Nodes of type MDI_STRING are immediately followed by a zero terminated char string.
// Nodes of type MDI_LIST are followed by the list's child nodes.
typedef struct {
    mdi_id_t    id;
    uint32_t    length;             // total length of the node, including subtree
    union {
        uint8_t     u8;             // also used for boolean values
        int32_t     i32;
        uint32_t    u32;
        uint64_t    u64;
        uint32_t    str_len;        // length of zero terminated string following this struct
        uint32_t    child_count;    // number of children for list nodes
    } value;
} mdi_node_t;
static_assert(sizeof(mdi_node_t) == 16, "");

// MDI header. This goes at the beginning and is immediately followed by root node.
typedef struct {
    uint32_t        magic;          // MDI_MAGIC
    uint32_t        length;         // total length of MDI data, including this header
    uint16_t        version_major;  // MDI format major version
    uint16_t        version_minor;  // MDI format minor version
    uint32_t        reserved;       // set to zero
} mdi_header_t;
static_assert(sizeof(mdi_header_t) == 16, "");

__END_CDECLS;
