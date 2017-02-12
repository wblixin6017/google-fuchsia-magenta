LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SO_NAME := qcom-glink

MODULE_CFLAGS := -Wno-strict-prototypes -Wno-discarded-qualifiers -Wno-shift-count-overflow -Wno-shift-count-overflow -Wno-unused-but-set-variable -Wno-sign-compare -Wno-unused-variable -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-unused-variable

MODULE_CFLAGS := -DGLINK_SUPPORT=1

MODULE_SRCS += \
	$(LOCAL_DIR)/rpm-ipc.c \
	$(LOCAL_DIR)/smem_list.c \
	$(LOCAL_DIR)/rpm-glink.c \
	$(LOCAL_DIR)/xport_rpm.c \
	$(LOCAL_DIR)/glink_api.c \
	$(LOCAL_DIR)/glink_os_utils_dal.c \
	$(LOCAL_DIR)/glink_rpmcore_setup.c \
	$(LOCAL_DIR)/glink_vector.c \
	$(LOCAL_DIR)/glink_core_intentless_xport.c \
	$(LOCAL_DIR)/glink_core_if.c \

#	$(LOCAL_DIR)/smd.c \
#	$(LOCAL_DIR)/smem.c \

MODULE_LIBS := \
    ulib/driver ulib/magenta ulib/musl

MODULE_STATIC_LIBS := \
    ulib/ddk \

include make/module.mk
