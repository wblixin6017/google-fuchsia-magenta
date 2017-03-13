# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/pcm.c \
    $(LOCAL_DIR)/codec/hifi-berry.c \


MODULE_STATIC_LIBS := ulib/bcm ulib/ddk

MODULE_LIBS := ulib/driver ulib/c ulib/magenta ulib/mxio

include make/module.mk
