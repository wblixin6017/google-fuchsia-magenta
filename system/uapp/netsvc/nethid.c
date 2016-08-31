// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netsvc.h"

#include <hexdump/hexdump.h>
#include <magenta/fuchsia-types.h>
#include <magenta/listnode.h>
#include <magenta/device/hidctl.h>

#include <mxio/io.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct {
    uint32_t next_dev;
    struct list_node devs;
} nethid_state = {
    .next_dev = 1,
    .devs = LIST_INITIAL_VALUE(nethid_state.devs),
};

struct nethid_dev {
    uint32_t id;
    int hidfd;
    size_t hid_rpt_desc_len;
    char* hid_rpt_desc;

    struct list_node node;
};

void nethid_open(uint32_t cookie,
                 const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    uint32_t ret = 0;
    char addr[IP6TOAMAX];
    ip6toa(addr, (void*)saddr);
    printf("nethid_open cookie %u saddr %s sport %u dport %u\n",
            cookie, addr, sport, dport);

    nbmsg m;
    struct nethid_dev* dev = calloc(1, sizeof(struct nethid_dev));
    if (dev == NULL) {
        printf("No memory for nethid\n");
        ret = ERR_NO_MEMORY;
        goto send;
    }
    dev->hidfd = open(HIDCTL_DEV, O_RDWR);
    if (dev->hidfd < 0) {
        printf("could not open %s: %d\n", HIDCTL_DEV, errno);
        free(dev);
        ret = errno;
        goto send;
    }
    dev->id = nethid_state.next_dev++;
    ret = dev->id;
    list_add_head(&nethid_state.devs, &dev->node);

send:
    m.magic = NB_MAGIC;
    m.cookie = cookie;
    m.cmd = NB_ACK;
    m.arg = ret;
    udp6_send(&m, sizeof(m), saddr, sport, dport);
}

void nethid_cfg(uint32_t arg, const char* data, size_t len, uint32_t cookie,
                 const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    char addr[IP6TOAMAX];
    ip6toa(addr, (void*)saddr);
    printf("nethid_cfg arg %u cookie %u saddr %s sport %u dport %u\n",
            arg, cookie, addr, sport, dport);
    hexdump(data, len);

    nbmsg m;
    uint32_t ret = 0;
    bool found = false;
    struct nethid_dev* dev;
    list_for_every_entry(&nethid_state.devs, dev, struct nethid_dev, node) {
        if (dev->id == arg) {
            found = true;
            break;
        }
    }
    if (!found) {
        ret = ERR_NOT_FOUND;
        goto send;
    }
    hid_ioctl_config_t* hid_cfg = (hid_ioctl_config_t*)data;
    dev->hid_rpt_desc_len = hid_cfg->rpt_desc_len;
    dev->hid_rpt_desc = calloc(1, hid_cfg->rpt_desc_len);
    memcpy(dev->hid_rpt_desc, hid_cfg->rpt_desc, hid_cfg->rpt_desc_len);
    int rc = mxio_ioctl(dev->hidfd, IOCTL_HID_CTL_CONFIG, hid_cfg, sizeof(*hid_cfg) + hid_cfg->rpt_desc_len, NULL, 0);
    if (rc < 0) {
        printf("hidctl ioctl failed: %d\n", rc);
        ret = rc;
        goto send;
    }

send:
    m.magic = NB_MAGIC;
    m.cookie = cookie;
    m.cmd = NB_ACK;
    m.arg = ret;
    udp6_send(&m, sizeof(m), saddr, sport, dport);
}

void nethid_report(uint32_t arg, const char* data, size_t len, uint32_t cookie,
                   const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    char addr[IP6TOAMAX];
    ip6toa(addr, (void*)saddr);
    printf("nethid_report arg %u cookie %u saddr %s sport %u dport %u\n",
            arg, cookie, addr, sport, dport);
    uint8_t rptlen = data[0];
    hexdump(&data[1], rptlen);

    nbmsg m;
    uint32_t ret = 0;
    bool found = false;
    struct nethid_dev* dev;
    list_for_every_entry(&nethid_state.devs, dev, struct nethid_dev, node) {
        if (dev->id == arg) {
            found = true;
            break;
        }
    }
    if (!found) {
        ret = ERR_NOT_FOUND;
        goto send;
    }
    ssize_t wrote = write(dev->hidfd, &data[1], data[0]);
    if (wrote < data[0]) {
        printf("could not write to hidctl dev: %zd\n", wrote);
        ret = wrote;
    }

send:
    m.magic = NB_MAGIC;
    m.cookie = cookie;
    m.cmd = NB_ACK;
    m.arg = ret;
    udp6_send(&m, sizeof(m), saddr, sport, dport);
}

void nethid_close(uint32_t arg, uint32_t cookie,
                  const ip6_addr_t* saddr, uint16_t sport, uint16_t dport) {
    char addr[IP6TOAMAX];
    ip6toa(addr, (void*)saddr);
    printf("nethid_close arg %u cookie %u saddr %s sport %u dport %u\n",
            arg, cookie, addr, sport, dport);

    bool found = false;
    struct nethid_dev* dev, *temp;
    list_for_every_entry_safe(&nethid_state.devs, dev, temp, struct nethid_dev, node) {
        if (dev->id == arg) {
            printf("closing hidfd\n");
            close(dev->hidfd);
            printf("freeing hid rpt desc\n");
            free(dev->hid_rpt_desc);
            printf("removing list node\n");
            list_delete(&dev->node);
            printf("freeing dev\n");
            free(dev);
            printf("break\n");
            found = true;
            break;
        }
    }

    nbmsg m;
    m.magic = NB_MAGIC;
    m.cookie = cookie;
    m.cmd = NB_ACK;
    m.arg = found ? 0 : ERR_NOT_FOUND;
    udp6_send(&m, sizeof(m), saddr, sport, dport);
}

