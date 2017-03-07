# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM := bcm28xx

#include make/module.mk

# build MDI
MDI_SRCS := \
    $(LOCAL_DIR)/rpi3.mdi \

MDI_DEPS := \
    kernel/include/mdi/kernel-defs.mdi \

include make/mdi.mk
