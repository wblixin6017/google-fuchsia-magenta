// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lk/init.h>
#include <stdio.h>
#include <trace.h>

#include "vmx.h"

#define LOCAL_TRACE 1

static void hyp_init(uint level) {
    LTRACE_ENTRY;

#if ARCH_X86
    status_t r = vmx_init();
    if (r < 0)
        return;
#endif

    LTRACE_EXIT;
}

LK_INIT_HOOK(hyp, hyp_init, LK_INIT_LEVEL_APPS);

