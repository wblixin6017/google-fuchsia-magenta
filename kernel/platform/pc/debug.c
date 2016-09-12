// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdarg.h>
#include <reg.h>
#include <stdio.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <lk/init.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <lib/cbuf.h>
#include <dev/interrupt.h>
#include <platform.h>
#include <platform/pc.h>
#include <platform/pc/memmap.h>
#include <platform/console.h>
#include <platform/debug.h>
#include <trace.h>

static const int uart_baud_rate = 115200;
static const int uart_io_port = 0x3f8;

cbuf_t console_input_buf;

enum handler_return platform_drain_debug_uart_rx(void)
{
    unsigned char c;
    bool resched = false;

    while (inp(uart_io_port + 5) & (1<<0)) {
        c = inp(uart_io_port + 0);
        cbuf_write_char(&console_input_buf, c, false);
        resched = true;
    }

    return resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

static enum handler_return uart_irq_handler(void *arg)
{
    //printf("irq\n");
    return platform_drain_debug_uart_rx();
}

void platform_init_debug_early(void)
{
    /* configure the uart */
    int divisor = 115200 / uart_baud_rate;

    /* get basic config done so that tx functions */
    outp(uart_io_port + 1, 0); // mask all irqs
    outp(uart_io_port + 3, 0x80); // set up to load divisor latch
    outp(uart_io_port + 0, divisor & 0xff); // lsb
    outp(uart_io_port + 1, divisor >> 8); // msb
    outp(uart_io_port + 3, 3); // 8N1
    outp(uart_io_port + 2, 0x07); // enable FIFO, clear, 14-byte threshold
}

void platform_init_debug(void)
{
    /* finish uart init to get rx going */
    cbuf_initialize(&console_input_buf, 1024);

    uint32_t irq = apic_io_isa_to_global(ISA_IRQ_SERIAL1);
    TRACEF("irq %u\n", irq);
    register_int_handler(irq, uart_irq_handler, NULL);
    unmask_interrupt(irq);

    //arch_disable_ints();

extern void api_io_debug(void);
    apic_io_debug();
extern void api_local_debug(void);
    apic_local_debug();

    outp(uart_io_port + 1, 0x1); // enable receive data available interrupt

#if 0
    arch_disable_ints();

    outp(0xcd6, 0x54);
    uint32_t val = inp(0xcd7);

    arch_enable_ints();

    printf("val 0x%x\n", val);
    printf("0x4d0 0x%x\n", inp(0x4d0));
#endif

#if 0
    for (;;) {
        printf("0x%x 0x%x\n", inp(uart_io_port + 2), inp(uart_io_port + 5));
    }
#endif

}

/* since the com1 IRQs do not work on pixel hardware, run a timer to poll for incoming
 * characters.
 */
static timer_t uart_rx_poll_timer;

static enum handler_return uart_rx_poll(struct timer *t, lk_time_t now, void *arg)
{
    return platform_drain_debug_uart_rx();
}

static void debug_irq_init(uint level)
{
    printf("Enabling Debug UART RX Hack\n");
    timer_initialize(&uart_rx_poll_timer);
    timer_set_periodic(&uart_rx_poll_timer, 10, uart_rx_poll, NULL);
}

LK_INIT_HOOK(uart_irq, debug_irq_init, LK_INIT_LEVEL_LAST);

static void debug_uart_putc(char c)
{
    while ((inp(uart_io_port + 5) & (1<<6)) == 0)
        ;
    outp(uart_io_port + 0, c);
}

void platform_dputc(char c)
{
    if (c == '\n')
        platform_dputc('\r');

    cputc(c);
    debug_uart_putc(c);
}

int platform_dgetc(char *c, bool wait)
{
    return cbuf_read_char(&console_input_buf, c, wait);
}
