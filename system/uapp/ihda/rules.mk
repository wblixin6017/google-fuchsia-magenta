# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_TYPE := userapp

MODULE_SRCS += \
    $(LOCAL_DIR)/ihda.cpp \
    $(LOCAL_DIR)/intel_hda_codec.cpp \
    $(LOCAL_DIR)/intel_hda_controller.cpp \
    $(LOCAL_DIR)/intel_hda_device.cpp \
    $(LOCAL_DIR)/magenta_device.cpp \
    $(LOCAL_DIR)/print_codec_state.cpp

MODULE_STATIC_LIBS := \
    udev/intel-hda/intel-hda-driver-utils

MODULE_LIBS := ulib/c \
               ulib/magenta \
               ulib/mxcpp \
               ulib/mxio \
               ulib/mxtl

include make/module.mk
