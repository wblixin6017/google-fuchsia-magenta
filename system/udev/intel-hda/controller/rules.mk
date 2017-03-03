# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := intel-hda-controller
MODULE_TYPE := driver
MODULE_CFLAGS := -I$(LOCAL_DIR)/include
MODULE_CPPFLAGS := $(MODULE_CFLAGS)

MODULE_STATIC_LIBS := \
    udev/intel-hda/intel-hda-driver-utils \
    ulib/ddk

MODULE_LIBS := \
    ulib/c \
    ulib/driver \
    ulib/magenta \
    ulib/mx \
    ulib/mxcpp \
    ulib/mxtl

MODULE_SRCS += \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/codec-cmd-job.cpp \
    $(LOCAL_DIR)/debug.cpp \
    $(LOCAL_DIR)/intel-hda-codec.cpp \
    $(LOCAL_DIR)/intel-hda-controller.cpp \
    $(LOCAL_DIR)/intel-hda-controller-init.cpp \
    $(LOCAL_DIR)/intel-hda-device.cpp \
    $(LOCAL_DIR)/intel-hda-stream.cpp \
    $(LOCAL_DIR)/irq-thread.cpp \
    $(LOCAL_DIR)/utils.cpp

include make/module.mk
