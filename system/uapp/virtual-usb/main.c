// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <magenta/device/usb-client.h>

#include <magenta/types.h>

#define DEV_VIRTUAL_USB "/dev/class/usb-client/000"

#define xprintf(fmt...) do { if (verbose) printf(fmt); } while (0)

static void usage(void) {
    printf("usage: virtual-usb <command> [<args>]\n\n");
    printf("  commands:\n");
    printf("    connect\n");
    printf("    disconnect\n");
}


int main(int argc, const char** argv) {
/*
    if (argc < 2) {
        usage();
        return 0;
    }
    argc--;
    argv++;
     if (!strcmp("read", argv[0])) {
        if (argc > 1) {
            return read_reports(argc, argv);
        } else {
            return readall_reports(argc, argv);
        }
    }
    if (!strcmp("get", argv[0])) return get_report(argc, argv);
    if (!strcmp("set", argv[0])) return set_report(argc, argv);
    usage();
*/

    int fd = open(DEV_VIRTUAL_USB, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "could not open %s\n", DEV_VIRTUAL_USB);
        return fd;
    }

    int connected = 1;
    mx_status_t status = ioctl_usb_client_set_connected(fd, &connected);
    printf("ioctl_usb_client_set_connected returned %d\n", status);
    close(fd);

    return 0;
}
