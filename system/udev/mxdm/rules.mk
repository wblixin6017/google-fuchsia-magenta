# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

SHARED_SRCS := \
    $(LOCAL_DIR)/bitmap.c \
    $(LOCAL_DIR)/cache.c \
    $(LOCAL_DIR)/device.c \
    $(LOCAL_DIR)/worker.c

# MXDM crypt

MODULE := mxdm-crypt

MODULE_TYPE := driver

MODULE_STATIC_LIBS := ulib/ddk

MODULE_LIBS := ulib/driver ulib/magenta ulib/musl

MODULE_SRCS := $(SHARED_SRCS) $(LOCAL_DIR)/$(MODULE).c

include make/module.mk

# MXDM verity

MODULE := mxdm-verity

MODULE_TYPE := driver

MODULE_STATIC_LIBS := ulib/ddk ulib/cryptolib

MODULE_LIBS := ulib/driver ulib/magenta ulib/musl

MODULE_SRCS := $(SHARED_SRCS) $(LOCAL_DIR)/$(MODULE).c

include make/module.mk
