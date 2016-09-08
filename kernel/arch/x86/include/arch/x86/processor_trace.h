// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS

void x86_processor_trace_init(void);
status_t x86_processor_trace_enable(uint capture_size_log2);
status_t x86_processor_trace_disable(
        paddr_t* capture_buf, size_t* buffer_size, size_t* capture_size);

__END_CDECLS
