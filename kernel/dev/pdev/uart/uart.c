// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <pdev/uart.h>

static int default_putc(int port, char c) {
    return -1;
}

static int default_getc(int port, bool wait) {
    return -1;
}

static void default_flush_tx(int port) {
}

static void default_flush_rx(int port) {
}

static void default_init_port(int port, uint baud) {
}


static int default_pputc(int port, char c) {
    return -1;
}

static int default_pgetc(int port) {
    return -1;
}

static const struct pdev_uart_ops default_ops = {
    .putc = default_putc,
    .getc = default_getc,
    .flush_tx = default_flush_tx,
    .flush_rx = default_flush_rx,
    .init_port = default_init_port,
    .pputc = default_pputc,
    .pgetc = default_pgetc,
};

static const struct pdev_uart_ops* uart_ops = &default_ops;

void uart_init(void) {
}

void uart_init_early(void) {
}

int uart_putc(int port, char c) {
    if (!uart_ops) return ERR_NOT_CONFIGURED;
    return uart_ops->putc(port, c);
}

int uart_getc(int port, bool wait)
{
    if (!uart_ops) return ERR_NOT_CONFIGURED;
    return uart_ops->getc(port, wait);
}

void uart_flush_tx(int port) {
    if (uart_ops) {
        uart_ops->flush_tx(port);
    }
}

void uart_flush_rx(int port) {
    if (uart_ops) {
        uart_ops->flush_rx(port);
    }
}

void uart_init_port(int port, uint baud) {
    if (uart_ops) {
        uart_ops->init_port(port, baud);
    }
}

int uart_pputc(int port, char c) {
    if (!uart_ops) return ERR_NOT_CONFIGURED;
    return uart_ops->pputc(port, c);
}

int uart_pgetc(int port) {
    if (!uart_ops) return ERR_NOT_CONFIGURED;
    return uart_ops->pgetc(port);
}

void pdev_register_uart(const struct pdev_uart_ops* ops) {
    uart_ops = ops;
}
