# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM := msm8998

DEVICE_TREE := $(LOCAL_DIR)/device-tree.dtb

# extra build rules for building fastboot compatible image
include make/fastboot.mk

# build MDI
MDI_SRCS := \
    $(LOCAL_DIR)/trapper.mdi \

MDI_DEPS := \
    kernel/include/mdi/kernel-defs.mdi \

EMBED_MDI:=true

include make/mdi.mk
