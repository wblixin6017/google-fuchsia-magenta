# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/audio2.cpp \
    $(LOCAL_DIR)/client-thread.cpp \
    $(LOCAL_DIR)/codec-driver-base.cpp \
    $(LOCAL_DIR)/driver-channel.cpp \
    $(LOCAL_DIR)/stream-base.cpp \
    $(LOCAL_DIR)/utils.cpp

MODULE_STATIC_LIBS := \
    ulib/ddk \
    ulib/mxcpp \
    ulib/mx \
    ulib/mxtl

include make/module.mk
