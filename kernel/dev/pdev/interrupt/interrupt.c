// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <pdev/interrupt.h>

static status_t default_mask(unsigned int vector) {
    return ERR_NOT_CONFIGURED;
}

static status_t default_unmask(unsigned int vector) {
    return ERR_NOT_CONFIGURED;
}

static status_t default_configure(unsigned int vector,
                          enum interrupt_trigger_mode tm,
                          enum interrupt_polarity pol) {
    return ERR_NOT_CONFIGURED;
}

static status_t default_get_config(unsigned int vector,
                           enum interrupt_trigger_mode* tm,
                           enum interrupt_polarity* pol) {
    return ERR_NOT_CONFIGURED;
}

static void default_register_handler(unsigned int vector, int_handler handler, void* arg) {
}

static bool default_is_valid(unsigned int vector, uint32_t flags) {
    return false;
}
static unsigned int default_remap(unsigned int vector) {
    return 0;
}

static status_t default_send_ipi(mp_cpu_mask_t target, mp_ipi_t ipi) {
    return ERR_NOT_CONFIGURED;
}

static void default_init_percpu(void) {
}

static const struct pdev_interrupt_ops default_ops = {
    .mask = default_mask,
    .unmask = default_unmask,
    .configure = default_configure,
    .get_config = default_get_config,
    .register_handler = default_register_handler,
    .is_valid = default_is_valid,
    .remap = default_remap,
    .send_ipi = default_send_ipi,
    .init_percpu = default_init_percpu,
};

static const struct pdev_interrupt_ops* intr_ops = &default_ops;

status_t mask_interrupt(unsigned int vector) {
    if (!intr_ops) return ERR_NOT_CONFIGURED;
    return intr_ops->mask(vector);
}

status_t unmask_interrupt(unsigned int vector) {
    if (!intr_ops) return ERR_NOT_CONFIGURED;
    return intr_ops->unmask(vector);
}

status_t configure_interrupt(unsigned int vector, enum interrupt_trigger_mode tm,
                             enum interrupt_polarity pol) {
    if (!intr_ops) return ERR_NOT_CONFIGURED;
    return intr_ops->configure(vector, tm, pol);
}

status_t get_interrupt_config(unsigned int vector, enum interrupt_trigger_mode* tm,
                              enum interrupt_polarity* pol) {
    if (!intr_ops) return ERR_NOT_CONFIGURED;
    return intr_ops->get_config(vector, tm, pol);
}

void register_int_handler(unsigned int vector, int_handler handler, void* arg) {
    if (!intr_ops) return;
    return intr_ops->register_handler(vector, handler, arg);
}

bool is_valid_interrupt(unsigned int vector, uint32_t flags) {
    if (!intr_ops) return false;
    return intr_ops->is_valid(vector, flags);
}

unsigned int remap_interrupt(unsigned int vector) {
    if (!intr_ops) return 0;
    return intr_ops->remap(vector);
}

status_t interrupt_send_ipi(mp_cpu_mask_t target, mp_ipi_t ipi) {
    if (!intr_ops) return ERR_NOT_CONFIGURED;
    return intr_ops->send_ipi(target, ipi);
}

void interrupt_init_percpu(void) {
    if (!intr_ops) {
        panic("no intr_ops in interrupt_init_percpu\n");
    }
    intr_ops->init_percpu();
}

void pdev_register_interrupts(const struct pdev_interrupt_ops* ops) {
    intr_ops = ops;
}
