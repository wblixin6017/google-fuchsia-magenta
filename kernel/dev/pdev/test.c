// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lk/init.h>
#include <pdev/driver.h>
#include <mdi/mdi-defs.h>
#include <mdi/mdi.h>

static void pdev_test_init(mdi_node_ref_t* node, uint level) {
    printf("pdev_test_init %x\n", level);

    mdi_node_ref_t test;
    uint32_t foo;
    if (mdi_find_node(node, MDI_KERNEL_DRIVERS_TEST_FOO, &test) != NO_ERROR ||
        mdi_node_uint32(&test, &foo) != NO_ERROR) {
        printf("pdev_test_init could not read MDI_KERNEL_DRIVERS_TEST_FOO\n");
        return;
    }
    printf("pdev_test_init foo = %u\n", foo);
}

LK_PDEV_INIT(pdev_test_init_early, MDI_KERNEL_DRIVERS_TEST, pdev_test_init, LK_INIT_LEVEL_PLATFORM_EARLY);
LK_PDEV_INIT(pdev_test_init, MDI_KERNEL_DRIVERS_TEST, pdev_test_init, LK_INIT_LEVEL_PLATFORM);
