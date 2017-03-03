# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := qemu-ihda-codec
MODULE_TYPE := driver

MODULE_STATIC_LIBS := \
    udev/intel-hda/intel-hda-driver-utils \
    ulib/ddk \
    ulib/mxtl

MODULE_LIBS := \
    ulib/c \
    ulib/driver \
    ulib/magenta \
    ulib/mx \
    ulib/mxcpp

MODULE_SRCS += \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/qemu-codec.cpp \
    $(LOCAL_DIR)/qemu-stream.cpp

include make/module.mk
