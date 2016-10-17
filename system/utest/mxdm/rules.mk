# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

UDEV_DIR := $(BUILDROOT)/system/udev/mxdm

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(UDEV_DIR)/bitmap.c \
    $(UDEV_DIR)/cache.c \
    $(UDEV_DIR)/device.c \
    $(UDEV_DIR)/worker.c \
    $(LOCAL_DIR)/device.c \
    $(LOCAL_DIR)/devmgr.c \
    $(LOCAL_DIR)/magenta.c \
    $(LOCAL_DIR)/mxdm.c

MODULE_COMPILEFLAGS += -I$(UDEV_DIR) -I$(LOCAL_DIR)

MODULE_NAME := mxdm-test

MODULE_STATIC_LIBS := ulib/ddk

MODULE_LIBS := ulib/unittest ulib/musl ulib/mxio

include make/module.mk
