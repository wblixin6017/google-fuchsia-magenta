// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdbool.h>
#include <magenta/types.h>
#include <magenta/mdi.h>

typedef struct mdi_node_ref {
    const mdi_node_t* node;
    uint32_t remaining_siblings;    // number of siblings following node in list
} mdi_node_ref_t;


// takes pointer to MDI data and returns reference to MDI root node
mx_status_t mdi_init(const void* mdi_data, size_t length, mdi_node_ref_t* out_ref);

#if EMBED_MDI
// returns reference to MDI root node from MDI embedded in kernel image
mx_status_t mdi_init_embedded(mdi_node_ref_t* out_ref);
#endif

// returns the type of a node
static inline mdi_id_t mdi_id(const mdi_node_ref_t* ref) {
    return ref->node->id;
}

// returns the type of a node
static inline mdi_type_t mdi_node_type(const mdi_node_ref_t* ref) {
    return MDI_ID_TYPE(ref->node->id);
}

// node value accessors
mx_status_t mdi_node_uint8(const mdi_node_ref_t* ref, uint8_t* out_value);
mx_status_t mdi_node_int32(const mdi_node_ref_t* ref, int32_t* out_value);
mx_status_t mdi_node_uint32(const mdi_node_ref_t* ref, uint32_t* out_value);
mx_status_t mdi_node_uint64(const mdi_node_ref_t* ref, uint64_t* out_value);
mx_status_t mdi_node_boolean(const mdi_node_ref_t* ref, bool* out_value);
const char* mdi_node_string(const mdi_node_ref_t* ref);

// list traversal
mx_status_t mdi_list_first_child(const mdi_node_ref_t* ref, mdi_node_ref_t* out_ref);
mx_status_t mdi_list_next_child(const mdi_node_ref_t* ref, mdi_node_ref_t* out_ref);
uint32_t mdi_node_child_count(const mdi_node_ref_t* ref);
mx_status_t mdi_list_find_node(const mdi_node_ref_t* ref, mdi_id_t id, mdi_node_ref_t* out_ref);

#define mdi_list_each_child(parent, child, status) \
    for ((status) = mdi_list_first_child(&parent, &child); (status) == NO_ERROR; \
         (status) = mdi_list_next_child(&child, &child)) 
