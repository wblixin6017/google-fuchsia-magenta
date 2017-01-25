# Copyright 2016 The Fuchsia Authors
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS := \
    $(LOCAL_DIR)/fdt.c \
    $(LOCAL_DIR)/fdt_addresses.c \
    $(LOCAL_DIR)/fdt_empty_tree.c \
    $(LOCAL_DIR)/fdt_ro.c \
    $(LOCAL_DIR)/fdt_rw.c \
    $(LOCAL_DIR)/fdt_strerror.c \
    $(LOCAL_DIR)/fdt_sw.c \
    $(LOCAL_DIR)/fdt_wip.c

MODULE_LIBS := ulib/musl

MODULE_COMPILEFLAGS += -Wno-sign-compare

MODULE_EXPORT := so

MODULE_SO_NAME := fdt

include make/module.mk
