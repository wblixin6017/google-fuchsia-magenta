// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "node.h"
#include "parser.h"

Node::Node(mdi_id_t id, std::string name)
    : id(id), id_name(name) {
}

void Node::print_indent(int depth) {
    for (int i = 0; i < depth; i++) {
        printf("    ");
    }
}

void Node::print_children(int depth) {
    for (auto iter = children.begin(); iter != children.end(); iter++) {
        iter->print(depth);
    }
}

void Node::print(int depth) {
    const char* name = nullptr;

    if (id_name.length() > 0) {
        name = id_name.c_str();
        const char* last_dot = strrchr(name, '.');
        if (last_dot) {
            name = last_dot + 1;
        }
    }

    print_indent(depth);

    if (name) {
        printf("%s = ", name);
    }

    switch (MDI_ID_TYPE(id)) {
    case MDI_INT32:
        printf("%d", (int)int_value);
        break;
    case MDI_UINT8:
    case MDI_UINT32:
        printf("%u", (unsigned int)int_value);
        break;
    case MDI_UINT64:
        printf("%" PRIu64, int_value);
        break;
    case MDI_BOOLEAN:
        printf("%s", (int_value ? "true" : "false"));
        break;
    case MDI_STRING:
        printf("%s", string_value.c_str());
        break;
    case MDI_LIST:
        printf("{\n");
        print_children(depth + 1);
        print_indent(depth);
        printf("}");
        break;
    case MDI_INVALID_TYPE:
        assert(0);
        break;
    }

    printf("\n");
}

void Node::compute_node_length() {
    switch (MDI_ID_TYPE(id)) {
    case MDI_UINT8:
    case MDI_INT32:
    case MDI_UINT32:
    case MDI_UINT64:
    case MDI_BOOLEAN:
        // primitive types are self contained
        serialized_length = sizeof(mdi_node_t);
        break;
    case MDI_STRING:
        // zero terminated string follows the mdi_node_t
        serialized_length = MDI_ALIGN(sizeof(mdi_node_t) + string_value.length() + 1);
        break;
    case MDI_LIST:
        serialized_length = sizeof(mdi_node_t);
        // recurse through our children
        for (auto iter = children.begin(); iter != children.end(); iter++) {
            iter->compute_node_length();
            serialized_length += iter->serialized_length;
        }
        break;
    case MDI_INVALID_TYPE:
    default:
        printf("invalid type %d\n", MDI_ID_TYPE(id));
        assert(0);
        break;
    }
}

bool Node::serialize(std::ofstream& out_file) {
    mdi_node_t node;
    static_assert(sizeof(node) == MDI_ALIGN(sizeof(node)), "");

    memset(&node, 0, sizeof(node));
    node.id = id;
    node.length = serialized_length;
    mdi_type_t type = MDI_ID_TYPE(id);

    switch (type) {
    case MDI_UINT8:
        node.value.u8 = (uint8_t)int_value;
        break;
    case MDI_INT32:
    case MDI_UINT32:
        node.value.u32 = (uint32_t)int_value;
        break;
    case MDI_UINT64:
        node.value.u64 = int_value;
        break;
    case MDI_BOOLEAN:
        node.value.u8 = int_value;
        break;
    case MDI_STRING:
        node.value.str_len = string_value.length() + 1;
        break;
    case MDI_LIST:
        node.value.child_count = children.size();
        break;
    case MDI_INVALID_TYPE:
    default:
        assert(0);
        return false;
    }

    out_file.write((const char *)&node, sizeof(node));

    // length following node that may need padding
    int pad_length = 0;

    if (type == MDI_STRING) {
        // string values are written immediately after the mdi_node_t
        int strlen = string_value.length() + 1;
        out_file.write(string_value.c_str(), strlen);
        // may need to pad following string value
        pad_length = strlen;
    } else if (type == MDI_LIST) {
        // children are recursively written following node
        for (auto iter = children.begin(); iter != children.end(); iter++) {
            iter->serialize(out_file);
        }
    }

    // align to MDI_ALIGNMENT boundary
    int pad = MDI_ALIGN(pad_length) - pad_length;
    if (pad) {
        char zeros[MDI_ALIGNMENT] = { 0 };
        out_file.write(zeros, pad);
    }

// TODO - error handling
    return true;
}
