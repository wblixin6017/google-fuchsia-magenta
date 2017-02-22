// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <magenta/compiler.h>
#include <magenta/syscalls.h>
#include <mdi/mdi.h>
#include <unittest/unittest.h>

#include "gen-mdi-test.h"

#define MDI_PATH "/boot/data/mditest.mdi"

static void* mdi_data = NULL;
static size_t mdi_length = 0;

static bool load_mdi(void) {
    BEGIN_TEST;

    int fd = open(MDI_PATH, O_RDONLY);
    EXPECT_GE(fd, 0, "Could not open " MDI_PATH);

    off_t length = lseek(fd, 0, SEEK_END);
    EXPECT_GT(fd, 0, "Could not determine length of " MDI_PATH);
    lseek(fd, 0, SEEK_SET);

    mdi_data = malloc(length);
    EXPECT_NONNULL(mdi_data, "Could not allocate memory to read " MDI_PATH);
    EXPECT_EQ(read(fd, mdi_data, length), length, "Could not read %s\n" MDI_PATH);
    mdi_length = length;

    close(fd);

    END_TEST;
}

bool simple_tests(void) {
    BEGIN_TEST;

    mdi_node_ref_t root, node;
    uint8_t u8;
    int32_t i32;
    uint32_t u32;
    uint64_t u64;
    bool b;

    EXPECT_EQ(mdi_init(mdi_data, mdi_length, &root), 0, "mdi_init failed");

    // uint8 test
    EXPECT_EQ(mdi_list_find_node(&root, MDI_TEST_UINT8, &node), 0,
              "MDI_TEST_UINT8 not found");
    EXPECT_EQ(mdi_node_uint8(&node, &u8), 0, "mdi_node_uint8 failed");
    EXPECT_EQ(u8, 123, "mdi_node_uint8 returned wrong value");

    // int32 test
    EXPECT_EQ(mdi_list_find_node(&root, MDI_TEST_INT32, &node), 0,
              "MDI_TEST_INT32 not found");
    EXPECT_EQ(mdi_node_int32(&node, &i32), 0, "mdi_node_int32 failed");
    EXPECT_EQ(i32, -123, "mdi_node_int32 returned wrong value");

    // uint32 test
    EXPECT_EQ(mdi_list_find_node(&root, MDI_TEST_UINT32, &node), 0,
              "MDI_TEST_UINT32 not found");
    EXPECT_EQ(mdi_node_uint32(&node, &u32), 0, "mdi_node_uint32 failed");
    EXPECT_EQ(u32, 0xFFFFFFFFu, "mdi_node_uint32 returned wrong value");

    // uint64 test
    EXPECT_EQ(mdi_list_find_node(&root, MDI_TEST_UINT64, &node), 0,
              "MDI_TEST_UINT64 not found");
    EXPECT_EQ(mdi_node_uint64(&node, &u64), 0, "mdi_node_uint64 failed");
    EXPECT_EQ(u64, 0x3FFFFFFFFu, "mdi_node_uint64 returned wrong value");

    // boolean test
    EXPECT_EQ(mdi_list_find_node(&root, MDI_TEST_BOOLEAN_TRUE, &node), 0,
              "MDI_TEST_BOOLEAN_TRUE not found");
    EXPECT_EQ(mdi_node_boolean(&node, &b), 0, "mdi_node_boolean failed");
    EXPECT_EQ(b, true, "mdi_node_boolean returned wrong value");
    EXPECT_EQ(mdi_list_find_node(&root, MDI_TEST_BOOLEAN_FALSE, &node), 0,
              "MDI_TEST_BOOLEAN_FALSE not found");
    EXPECT_EQ(mdi_node_boolean(&node, &b), 0, "mdi_node_boolean failed");
    EXPECT_EQ(b, false, "mdi_node_boolean returned wrong value");

    // string test
    EXPECT_EQ(mdi_list_find_node(&root, MDI_TEST_STRING, &node), 0,
              "MDI_TEST_STRING not found");
    const char* string = mdi_node_string(&node);
    ASSERT_NEQ(string, NULL, "mdi_node_string returned NULL");
    EXPECT_EQ(strcmp(string, "hello"), 0, "mdi_node_string failed");

    END_TEST;
}


bool list_tests(void) {
    BEGIN_TEST;

    mdi_node_ref_t root, node, child;
    int32_t i32;
    const char* string;
    mx_status_t status;

    const int32_t test_ints[] = {
        1, 2, 3
    };
    const char* test_strings[] = {
        "one", "two", "three"
    };

    EXPECT_EQ(mdi_init(mdi_data, mdi_length, &root), 0, "mdi_init failed");

    // test list array
    EXPECT_EQ(mdi_list_find_node(&root, MDI_TEST_LIST, &node), 0,
              "MDI_TEST_LIST_ARRAY not found");

    int i = 0;
    mdi_list_each_child(node, child, status) {
        mdi_node_ref_t grand_child;
        EXPECT_EQ(mdi_list_first_child(&child, &grand_child), 0, "mdi_list_first_child failed");
        EXPECT_EQ(mdi_node_type(&grand_child), (uint32_t)MDI_INT32, "expected type MDI_INT32");
        EXPECT_EQ(grand_child.node->id, (uint32_t)MDI_TEST_LIST_INT, "expected MDI_TEST_LIST_ARRAY_INT");
        EXPECT_EQ(mdi_node_int32(&grand_child, &i32), 0, "mdi_array_int32 failed");
        EXPECT_EQ(i32, test_ints[i], "mdi_node_int32 returned wrong value");
        EXPECT_EQ(mdi_list_next_child(&grand_child, &grand_child), 0, "mdi_list_next_child failed");
        EXPECT_EQ(mdi_node_type(&grand_child), (uint32_t)MDI_STRING, "expected type MDI_STRING");
        EXPECT_EQ(grand_child.node->id, (uint32_t)MDI_TEST_LIST_STR, "expected MDI_TEST_LIST_ARRAY_STR");
        string = mdi_node_string(&grand_child);
        ASSERT_NEQ(string, NULL, "mdi_node_string returned NULL");
        EXPECT_EQ(strcmp(string, test_strings[i]), 0, "mdi_node_string failed");
        // should be end of child list
        EXPECT_NEQ(mdi_list_next_child(&grand_child, &grand_child), 0, "mdi_list_next_child shouldn't have succeeded");

        i++;
    }

    END_TEST;
}

BEGIN_TEST_CASE(mdi_tests)
RUN_TEST(load_mdi);
RUN_TEST(simple_tests);
RUN_TEST(list_tests);
END_TEST_CASE(mdi_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
