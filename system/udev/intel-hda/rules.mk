# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

INTEL_HDA_DRIVER_BASE := $(GET_LOCAL_DIR)
include $(INTEL_HDA_DRIVER_BASE)/controller/rules.mk
include $(INTEL_HDA_DRIVER_BASE)/intel-hda-driver-utils/rules.mk
