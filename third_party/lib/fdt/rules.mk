LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

SRC_DIR := third_party/ulib/fdt

KERNEL_INCLUDES += $(SRC_DIR)/include

MODULE_SRCS += \
    $(SRC_DIR)/fdt.c \
    $(SRC_DIR)/fdt_addresses.c \
    $(SRC_DIR)/fdt_empty_tree.c \
    $(SRC_DIR)/fdt_ro.c \
    $(SRC_DIR)/fdt_rw.c \
    $(SRC_DIR)/fdt_strerror.c \
    $(SRC_DIR)/fdt_sw.c \
    $(SRC_DIR)/fdt_wip.c

MODULE_COMPILEFLAGS += -Wno-sign-compare

include make/module.mk
