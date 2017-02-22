// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>

#include <mdi/mdi.h>

#define VERSION_MAJOR   1

#define DEBUG   1

#if DEBUG
#ifdef _KERNEL
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) fprintf(stderr, fmt)
#endif
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#if EMBED_MDI
extern const uint8_t embedded_mdi[];
extern const uint32_t embedded_mdi_len;
#endif

// takes pointer to MDI header and returns reference to MDI root node
mx_status_t mdi_init(const void* mdi_data, size_t length, mdi_node_ref_t* out_ref) {
    const mdi_header_t* header = (const mdi_header_t *)mdi_data;
    // length should be at least size of header plus one node, and length should match header length
    if (length < sizeof(mdi_node_t) + sizeof(mdi_node_t) || length != header->length) {
        xprintf("%s: bad length\n", __FUNCTION__);
        return ERR_INVALID_ARGS;
    }
    if (header->magic != MDI_MAGIC) {
        xprintf("%s: bad magic 0x%08X\n", __FUNCTION__, header->magic);
        return ERR_INVALID_ARGS;
    }
    if (header->version_major != VERSION_MAJOR) {
        xprintf("%s: unsupported version %d.%d\n", __FUNCTION__, header->version_major,
                header->version_minor);
        return ERR_INVALID_ARGS;
    }

    out_ref->node = (const mdi_node_t *)((const char *)header + sizeof(*header));
    out_ref->remaining_siblings = 0;
    return NO_ERROR;
}

#if EMBED_MDI
// returns reference to MDI root node from MDI embedded in kernel image
mx_status_t mdi_init_embedded(mdi_node_ref_t* out_ref) {
    return mdi_init(embedded_mdi, embedded_mdi_len, out_ref);
}
#endif

mx_status_t mdi_node_uint8(const mdi_node_ref_t* ref, uint8_t* out_value) {
    if (mdi_node_type(ref) != MDI_UINT8) {
        xprintf("%s: bad node type for mdi_node_uint8\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    *out_value = ref->node->value.u8;
    return NO_ERROR;
}

mx_status_t mdi_node_int32(const mdi_node_ref_t* ref, int32_t* out_value) {
    if (mdi_node_type(ref) != MDI_INT32) {
        xprintf("%s: bad node type for mdi_node_int32\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    *out_value = ref->node->value.i32;
    return NO_ERROR;
}

mx_status_t mdi_node_uint32(const mdi_node_ref_t* ref, uint32_t* out_value) {
    if (mdi_node_type(ref) != MDI_UINT32) {
        xprintf("%s: bad node type for mdi_node_uint32\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    *out_value = ref->node->value.u32;
    return NO_ERROR;
}

mx_status_t mdi_node_uint64(const mdi_node_ref_t* ref, uint64_t* out_value) {
    if (mdi_node_type(ref) != MDI_UINT64) {
        xprintf("%s: bad node type for mdi_node_uint64\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    *out_value = ref->node->value.u64;
    return NO_ERROR;
}

mx_status_t mdi_node_boolean(const mdi_node_ref_t* ref, bool* out_value) {
    if (mdi_node_type(ref) != MDI_BOOLEAN) {
        xprintf("%s: bad node type for mdi_node_boolean\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    *out_value = !!ref->node->value.u8;
    return NO_ERROR;
}

const char* mdi_node_string(const mdi_node_ref_t* ref) {
    if (mdi_node_type(ref) != MDI_STRING) {
        xprintf("%s: bad node type for mdi_string_value\n", __FUNCTION__);
        return NULL;
    }
    return (const char *)ref->node + sizeof(*ref->node);
}

mx_status_t mdi_list_first_child(const mdi_node_ref_t* ref, mdi_node_ref_t* out_ref) {
    out_ref->node = NULL;
    out_ref->remaining_siblings = 0;

   if (mdi_node_type(ref) != MDI_LIST) {
        xprintf("%s: ref not a list in mdi_list_first_child\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    if (ref->node->value.child_count == 0) {
        return ERR_NOT_FOUND;
    }
    // first child immediately follows list node
    out_ref->node = &ref->node[1];
    out_ref->remaining_siblings = ref->node->value.child_count - 1;
    return NO_ERROR;
}

mx_status_t mdi_list_next_child(const mdi_node_ref_t* ref, mdi_node_ref_t* out_ref) {
    if (ref->remaining_siblings == 0) {
        out_ref->node = NULL;
        out_ref->remaining_siblings = 0;
        return ERR_NOT_FOUND;
    }

    out_ref->node = (mdi_node_t *)((char *)ref->node + ref->node->length);
    out_ref->remaining_siblings = ref->remaining_siblings - 1;
    return NO_ERROR;
}

uint32_t mdi_node_child_count(const mdi_node_ref_t* ref) {
    if (mdi_node_type(ref) == MDI_LIST) {
        return ref->node->value.child_count;
    } else {
        return 0;
    }
}

mx_status_t mdi_list_find_node(const mdi_node_ref_t* ref, mdi_id_t id, mdi_node_ref_t* out_ref) {
    out_ref->remaining_siblings = 0;
    mx_status_t status = mdi_list_first_child(ref, out_ref);

    while (status == NO_ERROR && out_ref->node->id != id) {
        status = mdi_list_next_child(out_ref, out_ref);
    }
    if (status != NO_ERROR) {
        out_ref->node = NULL;
    }
    return status;
}
