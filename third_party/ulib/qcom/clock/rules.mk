LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SO_NAME := qcom-clock

MODULE_CFLAGS := -Wno-strict-prototypes -Wno-discarded-qualifiers -Wno-shift-count-overflow -Wno-shift-count-overflow -Wno-unused-but-set-variable -Wno-sign-compare -Wno-unused-variable -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-unused-variable

MODULE_SRCS += \
	$(LOCAL_DIR)/clock.c \
	$(LOCAL_DIR)/clock_alpha_pll.c \
	$(LOCAL_DIR)/clock_lib2.c \
	$(LOCAL_DIR)/clock_pll.c \
	$(LOCAL_DIR)/msm8996-clock.c \

MODULE_LIBS := \
    ulib/driver ulib/magenta ulib/musl

MODULE_STATIC_LIBS := \
    ulib/ddk \

include make/module.mk
