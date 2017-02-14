// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <reg.h>
#include <err.h>
#include <debug.h>
#include <trace.h>

#include <dev/interrupt.h>
#include <kernel/event.h>

#include <platform/msm8998.h>
#include "platform_p.h"

#define GLINK_RPM_BASE     (MSM8998_PERIPH_BASE_VIRT + 0x00778000)
#define GLINK_RPM_SIZE     (0x7000)

#define GLINK_RPM_TOC_SIZE (256)
#define GLINK_RPM_TOC_BASE (GLINK_RPM_BASE + GLINK_RPM_SIZE - GLINK_RPM_TOC_SIZE)

#define GLINK_RPM_TOC_MAGIC    0x67727430 // '0trg'
#define GLINK_RPM_TXFIFO_MAGIC 0x61703272 // 'pa2r'
#define GLINK_RPM_RXFIFO_MAGIC 0x72326170 // 'r2pa'

#define FIFO_FULL_RESERVE      8
#define TX_BLOCKED_CMD_RESERVE 8 // sizeof(struct read_notif_request)

#define GLINK_RPM_IRQ_BASE (MSM8998_PERIPH_BASE_VIRT + 0x17911008)
#define GLINK_RPM_IRQ_MASK (0x1)

#define LOCAL_TRACE 1

enum glink_command_types {
    VERSION_CMD,
    VERSION_ACK_CMD,
    OPEN_CMD,
    CLOSE_CMD,
    OPEN_ACK_CMD,
    RX_INTENT_CMD,
    RX_DONE_CMD,
    RX_INTENT_REQ_CMD,
    RX_INTENT_REQ_ACK_CMD,
    TX_DATA_CMD,
    ZERO_COPY_TX_DATA_CMD,
    CLOSE_ACK_CMD,
    TX_DATA_CONT_CMD,
    READ_NOTIF_CMD,
    RX_DONE_W_REUSE_CMD,
    SIGNALS_CMD,
    TRACER_PKT_CMD,
    TRACER_PKT_CONT_CMD,
};

struct glink_command {
    uint16_t id;
    uint16_t version;
    uint32_t features;
};

struct glink_rpm_toc_entry {
    uint32_t magic;
    uint32_t desc_offset;
    uint32_t fifo_size;
};

struct glink_rpm_toc {
    uint32_t magic;
    uint32_t count;
    struct glink_rpm_toc_entry entries[20];
    char reserved[8]; // GLINK_RPM_TOC_SIZE (256) bytes
};

struct channel_desc {
    volatile uint32_t read_index;
    volatile uint32_t write_index;
};

static struct channel_desc *tx_channel_desc = NULL;
static uintptr_t tx_fifo;
static uint32_t tx_fifo_size;
static event_t tx_event;

static volatile struct channel_desc *rx_channel_desc = NULL;
static uintptr_t rx_fifo;
static uint32_t rx_fifo_size;
static event_t rx_event;

static void send_irq(void) {
    *REG32(GLINK_RPM_IRQ_BASE) = GLINK_RPM_IRQ_MASK;
}

static void* memcpy32(void* dst, const void* src, uint32_t count)
{
    uint32_t* dstw = (uint32_t*)dst;
    uint32_t* srcw = (uint32_t*)src;
    uint32_t* countw = count / sizeof(uint32_t);
    while (countw--) {
        *dstw++ = *srcw++;
    }
    return dst;
}

static uint32_t fifo_read_avail(struct channel_desc* desc, uint32_t fifo_size)
{
    uint32_t rindex = desc->read_index;
    uint32_t windex = desc->write_index;
    uint32_t bytes = windex - rindex;
    if (windex < rindex) {
        bytes += fifo_size;
    }
    return bytes;
}

static uint32_t fifo_write_avail(struct channel_desc* desc, uint32_t fifo_size)
{
    uint32_t rindex = desc->read_index;
    uint32_t windex = desc->write_index;
    uint32_t bytes = rindex - windex;
    if (rindex < windex) {
        bytes += fifo_size;
    }
    if (bytes < FIFO_FULL_RESERVE + TX_BLOCKED_CMD_RESERVE) {
        bytes = 0;
    } else {
        bytes -= FIFO_FULL_RESERVE + TX_BLOCKED_CMD_RESERVE;
    }
    return bytes;
}

static void glink_fifo_rx(void* data, uint32_t count)
{
    struct channel_desc* desc = rx_channel_desc;
    uint32_t rindex = desc->read_index;
    uint32_t windex = desc->write_index;
    uint32_t avail = fifo_read_avail(desc, rx_fifo_size);
    if (avail != count) {
        printf("glink: not enough data in fifo (want %u)\n", count);
        // TODO send signal and wait
        return;
    }
    desc->read_index = rindex;
}

static void glink_fifo_tx(const void* data, uint32_t count)
{
    struct channel_desc* desc = tx_channel_desc;
    uint32_t avail = fifo_write_avail(desc, tx_fifo_size);
    TRACEF("%u bytes available in tx fifo\n", n);
    if (count == 0) {
        return;
    }
    if (avail < count) {
        printf("glink: not enough space in fifo\n");
        // TODO send signal and wait
        return;
    }
    if ((count % sizeof(uint32_t)) != 0) {
        printf("number of bytes to tx must be word aligned (%u)\n", count);
        return;
    }
    uint32_t windex = desc->write_index;
    if (windex + count > tx_fifo_size) {
        uint32_t n = tx_fifo_size - windex;
        memcpy32((void*)(tx_fifo + windex), data, n);
        memcpy32((void*)(tx_fifo), data + n, count - n);
    } else {
        memcpy32((void*)(tx_fifo + windex), data, count);
    }
    windex += count;
    if (windex > tx_fifo_size) {
        windex -= tx_fifo_size;
    }
    desc->write_index = windex;
    // send irq to remote
    wmb();
    send_irq();
}

static enum handler_return glink_rpm_irq(void* arg)
{
    struct glink_command cmd;
    glink_fifo_rx(&cmd, sizeof(cmd));

    hexdump(cmd, sizeof(cmd));

    return INT_NO_RESCHEDULE;
}

static void glink_link_up(void)
{
    struct glink_command cmd = {
        .id = VERSION_CMD,
        .version = 1,
        .features = (1 << 2), // TRACER_PKT_FEATURE
    };
    glink_fifo_tx(&cmd, sizeof(cmd));
}

status_t glink_rpm_init(void)
{
    LTRACE_ENTRY;

    size_t toc_words = GLINK_RPM_TOC_SIZE >> 2;
    struct glink_rpm_toc toc;
    size_t i = 0;
    uint32_t* ptr = (uint32_t*)(&toc);
    do {
        *(ptr + i) = *REG32(GLINK_RPM_TOC_BASE + (i << 2));
        i += 1;
    } while (i < toc_words);

    if (toc.magic != GLINK_RPM_TOC_MAGIC) {
        return -1;
    }

    for (i = 0; i < toc.count; i += 1) {
        if (toc.entries[i].magic == GLINK_RPM_TXFIFO_MAGIC) {
            tx_channel_desc = (struct channel_desc*)(uintptr_t)(GLINK_RPM_BASE + toc.entries[i].desc_offset);
            tx_channel_desc->write_index = 0;
            tx_fifo = (uintptr_t)(tx_channel_desc + 1);
            tx_fifo_size = toc.entries[i].fifo_size;
        } else if (toc.entries[i].magic == GLINK_RPM_RXFIFO_MAGIC) {
            rx_channel_desc = (struct channel_desc*)(uintptr_t)(GLINK_RPM_BASE + toc.entries[i].desc_offset);
            rx_channel_desc->read_index = 0;
            rx_fifo = (uintptr_t)(rx_channel_desc + 1);
            rx_fifo_size = toc.entries[i].fifo_size;
        }
    }

    if (tx_channel_desc == NULL || rx_channel_desc == NULL) {
        return -1;
    }

    event_init(&tx_event);
    event_init(&rx_event);

    register_int_handler(GLINK_RPM_INT, &glink_rpm_irq, NULL);
    unmask_irq(GLINK_RPM_INT);

    glink_link_up();

    LTRACE_EXIT;

    return NO_ERROR;
}
