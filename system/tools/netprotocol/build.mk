# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

NETRUNCMD := $(BUILDDIR)/tools/netruncmd
NETCP := $(BUILDDIR)/tools/netcp
MDM := $(BUILDDIR)/tools/mdm

TOOLS := $(NETRUNCMD) $(NETCP) $(MDM)

$(NETRUNCMD): $(LOCAL_DIR)/netruncmd.c $(LOCAL_DIR)/netprotocol.c $(LOCAL_DIR)/configuration.c
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CC) $(HOST_COMPILEFLAGS) $(HOST_CFLAGS) -o $@ $^

$(NETCP): $(LOCAL_DIR)/netcp.c $(LOCAL_DIR)/netprotocol.c $(LOCAL_DIR)/configuration.c
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CC) $(HOST_COMPILEFLAGS) $(HOST_CFLAGS) -o $@ $^

$(MDM): $(LOCAL_DIR)/mdm.c $(LOCAL_DIR)/netprotocol.c $(LOCAL_DIR)/configuration.c
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CC) $(HOST_COMPILEFLAGS) $(HOST_CFLAGS) -o $@ $^

GENERATED += $(TOOLS)
EXTRA_BUILDDEPS += $(TOOLS)

# phony rule to build just the tools
.PHONY: tools
tools: $(TOOLS)
