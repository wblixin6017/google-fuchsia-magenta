// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <dev/interrupt.h>

__BEGIN_CDECLS

// Interrupt Controller interface
struct pdev_interrupt_ops {
    status_t (*mask)(unsigned int vector);
    status_t (*unmask)(unsigned int vector);
    status_t (*configure)(unsigned int vector,
                          enum interrupt_trigger_mode tm,
                          enum interrupt_polarity pol);
    status_t (*get_config)(unsigned int vector,
                           enum interrupt_trigger_mode* tm,
                           enum interrupt_polarity* pol);
    void (*register_handler)(unsigned int vector, int_handler handler, void* arg);
    bool (*is_valid)(unsigned int vector, uint32_t flags);
    unsigned int (*remap)(unsigned int vector);
    status_t (*send_ipi)(mp_cpu_mask_t target, mp_ipi_t ipi);
    void (*init_percpu)(void);
};

void pdev_register_interrupts(const struct pdev_interrupt_ops* ops);

__END_CDECLS
