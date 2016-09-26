// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <stdint.h>

#include <magenta/compiler.h>

__BEGIN_CDECLS

void _x86_suspend_wakeup(void) __NO_RETURN;
void x86_suspend_wakeup(void* usermode_aspace, uint64_t usermode_ip, void* bootstrap_aspace) __NO_RETURN;

__END_CDECLS
