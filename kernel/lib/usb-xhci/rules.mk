# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

SRC_DIR := system/ulib/usb-xhci

MODULE_CFLAGS := -DKERNEL

KERNEL_INCLUDES += $(SRC_DIR)/include

MODULE_SRCS := \
    $(SRC_DIR)/xhci.c \
    $(SRC_DIR)/xhci-device-manager.c \
    $(SRC_DIR)/xhci-root-hub.c \
    $(SRC_DIR)/xhci-transfer.c \
    $(SRC_DIR)/xhci-trb.c \
    $(SRC_DIR)/xhci-util.c \

include make/module.mk
