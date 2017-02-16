# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

TFTP_CFLAGS += -Werror-implicit-function-declaration
TFTP_CFLAGS += -Wstrict-prototypes -Wwrite-strings
TFTP_CFLAGS += -Isystem/ulib/system/include
TFTP_CFLAGS += -Isystem/ulib/magenta/include
TFTP_CFLAGS += -Isystem/ulib/mxio/include
TFTP_CFLAGS += -Isystem/ulib/mxtl/include
TFTP_CFLAGS += -Isystem/ulib/tftp/include
TFTP_CFLAGS += -Isystem/public
TFTP_CFLAGS += -Isystem/private

ifeq ($(HOST_PLATFORM),darwin)
TFTP_CFLAGS += -DO_DIRECTORY=0200000
else
TFTP_CFLAGS += -D_POSIX_C_SOURCE=200809L
endif

MINFS_LDFLAGS :=

SRCS := main.c tftp.c

OBJS := $(patsubst %.c,$(BUILDDIR)/host/system/ulib/tftp/%.c.o,$(SRCS))
DEPS := $(patsubst %.c,$(BUILDDIR)/host/system/ulib/tftp/%.c.d,$(SRCS))
TFTP_TOOLS := $(BUILDDIR)/tools/tftp

.PHONY: tftp
tftp: $(TFTP_TOOLS)

-include $(DEPS)

$(OBJS): $(BUILDDIR)/host/%.c.o: %.c
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CC) -MMD -MP $(HOST_COMPILEFLAGS) $(HOST_CFLAGS) $(TFTP_CFLAGS) -c -o $@ $<

$(BUILDDIR)/tools/tftp: $(OBJS)
	@echo linking $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CC) $(MINFS_LDFLAGS) -o $@ $^

GENERATED += $(OBJS)
GENERATED += $(TFTP_TOOLS)
EXTRA_BUILDDEPS += $(TFTP_TOOLS)
