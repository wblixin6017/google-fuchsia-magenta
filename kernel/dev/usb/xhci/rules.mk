# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_CFLAGS := -DKERNEL

MODULE_SRCS += \
	$(LOCAL_DIR)/xhci.c \

MODULE_DEPS += \
	dev/pcie \
	lib/xhci \

include make/module.mk
