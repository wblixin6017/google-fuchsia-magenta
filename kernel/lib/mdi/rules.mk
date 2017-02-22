# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

SRC_DIR := system/ulib/mdi
LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

KERNEL_INCLUDES += $(SRC_DIR)/include

MODULE_SRCS := \
    $(SRC_DIR)/mdi.c \

ifeq ($(EMBED_MDI),true)
MODULE_SRCDEPS += $(MDI_BIN)
MODULE_COMPILEFLAGS += -DEMBEDDED_MDI_FILENAME="\"$(MDI_BIN)\""
MODULE_SRCS += $(LOCAL_DIR)/embed-mdi.S
endif

include make/module.mk
