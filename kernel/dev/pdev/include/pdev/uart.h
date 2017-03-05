// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <dev/uart.h>

__BEGIN_CDECLS

// Interrupt Controller interface
struct pdev_uart_ops {
    int (*putc)(int port, char c);
    int (*getc)(int port, bool wait);
    void (*flush_tx)(int port);
    void (*flush_rx)(int port);
    void (*init_port)(int port, uint baud);

    /* panic-time uart accessors, intended to be run with interrupts disabled */
    int (*pputc)(int port, char c);
    int (*pgetc)(int port);
};

void pdev_register_uart(const struct pdev_uart_ops* ops);

__END_CDECLS
