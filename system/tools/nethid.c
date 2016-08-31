// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netprotocol.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <stdint.h>

#include <magenta/device/hidctl.h>
#include <magenta/netboot.h>

#include <linux/hidraw.h>
#include <sys/ioctl.h>

static const char* appname;

int main(int argc, char** argv) {
    appname = argv[0];

    if (argc < 3) {
        fprintf(stderr, "usage: %s <hostname> <command>\n", appname);
        return -1;
    }

    const char* hostname = argv[1];
    if (!strcmp(hostname, "-") || !strcmp(hostname, ":")) {
        hostname = "*";
    }

    int hidfd = open(argv[2], O_RDONLY);
    if (hidfd < 0) {
        perror("could not open hidraw device");
        return -1;
    }

    int desc_sz = 0;
    struct hidraw_report_descriptor rpt_desc;
    memset(&rpt_desc, 0, sizeof(rpt_desc));
    int rc = ioctl(hidfd, HIDIOCGRDESCSIZE, &desc_sz);
    if (rc < 0) {
        perror("HIDIOCGRDESCSIZE");
        return -1;
    }
    rpt_desc.size = desc_sz;
    rc = ioctl(hidfd, HIDIOCGRDESC, &rpt_desc);
    if (rc < 0) {
        perror("HIDIOCGRDESC");
        return -1;
    }
    fprintf(stderr, "Report Descriptor:\n");
    for (int i = 0; i < desc_sz; i++) {
        fprintf(stderr, "%02x ", rpt_desc.value[i]);
        if (i % 32 == 31) fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");

    hid_ioctl_config_t* hid_cfg = calloc(1, sizeof(hid_ioctl_config_t) + desc_sz);
    hid_cfg->dev_num = 1;
    hid_cfg->boot_device = false;
    hid_cfg->dev_class = 1;
    hid_cfg->rpt_desc_len = desc_sz;
    memcpy(hid_cfg->rpt_desc, rpt_desc.value, desc_sz);

    int s;
    if ((s = netboot_open(hostname, NB_SERVER_PORT, NULL)) < 0) {
        if (errno == ETIMEDOUT) {
            fprintf(stderr, "%s: lookup timed out\n", appname);
        }
        return -1;
    }

    msg m;
    m.hdr.magic = NB_MAGIC;
    m.hdr.cookie = 0x11224455;
    m.hdr.cmd = NB_HID_OPEN;
    m.hdr.arg = 0;
    //memcpy(m.data, cmd, cmd_len);

    msg r;
    memset(&r, 0, sizeof(r));
    rc = netboot_txn(s, &r, &m, sizeof(m));

    fprintf(stderr, "resp: rc=%d cmd=%u arg=%u\n", rc, r.hdr.cmd, r.hdr.arg);

    m.hdr.cmd = NB_HID_CFG;
    m.hdr.arg = r.hdr.arg;
    memcpy(m.data, hid_cfg, sizeof(*hid_cfg) + desc_sz);
    rc = netboot_txn(s, &r, &m, sizeof(m) + sizeof(*hid_cfg) + desc_sz);
    fprintf(stderr, "resp: rc=%d cmd=%u arg=%u\n", rc, r.hdr.cmd, r.hdr.arg);

    m.hdr.cmd = NB_HID_REPORT;
    memset(m.data, 0, sizeof(m.data));
    uint8_t buf[8];
    m.data[0] = sizeof(buf);
    while ((rc = read(hidfd, buf, sizeof(buf))) == sizeof(buf)) {
        fprintf(stderr, "read %d bytes\n", rc);
        if (rc > 0) {
            for (int i = 0; i < rc; i++) {
                fprintf(stderr, "%02x ", buf[i]);
            }
            fprintf(stderr, "\n");
        }
        memcpy(&m.data[1], buf, sizeof(buf));
        rc = netboot_txn(s, &r, &m, sizeof(m) + sizeof(buf) + 1);
        fprintf(stderr, "resp: rc=%d cmd=%u arg=%u\n", rc, r.hdr.cmd, r.hdr.arg);
        if (rc < 0) break;
    }
    printf("read rc: %d/%d\n", rc, errno);

    //struct termios oldt, newt;
    //tcgetattr(STDIN_FILENO, &oldt);
    //newt = oldt;
    //cfmakeraw(&newt);
    //tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    //int c = getchar();
    //tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    //fprintf(stderr, "c=%d\n", c);

    m.hdr.cmd = NB_HID_CLOSE;
    rc = netboot_txn(s, &r, &m, sizeof(m));
    fprintf(stderr, "resp: rc=%d cmd=%u arg=%u\n", rc, r.hdr.cmd, r.hdr.arg);

    close(s);

    return 0;
}
