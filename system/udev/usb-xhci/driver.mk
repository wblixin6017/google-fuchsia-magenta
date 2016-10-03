LOCAL_DIR := $(GET_LOCAL_DIR)

DRIVER_SRCS += \
    $(LOCAL_DIR)/usb-xhci.c \

MODULE_STATIC_LIBS += ulib/usb-xhci
