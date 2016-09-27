// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <acpica/acpi.h>

extern void x86_suspend_resume(void);
extern ACPI_STATUS x86_do_suspend(void);
