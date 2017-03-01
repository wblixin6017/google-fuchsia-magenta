// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "configuration.h"
#include "netprotocol.h"

static void usage(const char* appname) {
    fprintf(stderr,
            "usage:   %s [ <option> ]* <command> [ <commandoptions> ]*\n"
            "\n"
            "options:\n"
            "  -i <ifname> interface name\n"
            "\n"
            "commands:\n"
            "  devices list known and discovered devices\n"
            "  add <nodename> add device to trusted list\n"
            "  remove <nodename> remove device from trusted list\n",
            appname);
    exit(1);
}

static bool on_device(device_info_t* device, void* cookie) {
    configuration_t* config = cookie;
    device_info_t* known_device = config->get_device(config, device->nodename);
    device_info_t* new_device = config->add_device(config, device);
    if (!known_device) {
        new_device->ops |= OPS_DISCOVERED;
    }
    return true;
}

static bool discover_bootserver(on_device_cb callback, void* data) {
    struct sockaddr_in6 addr;
    char tmp[INET6_ADDRSTRLEN];
    int r, s, n = 1;

    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(NB_ADVERT_PORT);

    s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        fprintf(stderr, "cannot create socket %d\n", s);
        return false;
    }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if ((r = bind(s, (void*)&addr, sizeof(addr))) < 0) {
        fprintf(stderr, "cannot bind to [%s]%d %d: %s\n",
                inet_ntop(AF_INET6, &addr.sin6_addr, tmp, sizeof(tmp)),
                ntohs(addr.sin6_port), errno, strerror(errno));
        return false;
    }

    for (uint32_t i = 15; i > 0; --i) {
        struct sockaddr_in6 ra;
        socklen_t rlen;
        char buf[4096];
        nbmsg* msg = (void*)buf;
        rlen = sizeof(ra);
        r = recvfrom(s, buf, sizeof(buf) - 1, 0, (void*)&ra, &rlen);
        if (r < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            break;
        }
        if (r < sizeof(nbmsg))
            continue;
        if (!IN6_IS_ADDR_LINKLOCAL(&ra.sin6_addr)) {
            continue;
        }
        if (msg->magic != NB_MAGIC)
            continue;
        if (msg->cmd != NB_ADVERTISE)
            continue;

        if (!strncmp((const char*)msg->data, "nodename=", 9)) {
            inet_ntop(AF_INET6, &ra.sin6_addr, tmp, sizeof(tmp));

            const char* node = (void*)msg->data + 9;
            device_info_t info;
            strncpy(info.nodename, node, MAX_NODENAME);
            strncpy(info.inet6_addr_s, tmp, INET6_ADDRSTRLEN);
            memcpy(&info.inet6_addr, &ra, sizeof(ra));
            info.state = BOOTLOADER;
            info.bootloader_port = ntohs(ra.sin6_port);
            info.bootloader_version = msg->arg;
            if (!callback(&info, data)) {
                break;
            }
        }
    }
    return true;
}

static void list_devices(configuration_t* config) {
    if (netboot_discover(NB_SERVER_PORT, NULL, on_device, config)) {
        fprintf(stderr, "Failed to discover\n");
    }

    if (!discover_bootserver(on_device, config)) {
        fprintf(stderr, "Failed to discover\n");
    }

    fprintf(stdout, "%d device(s)\n", config->devices_count);
    for (int i = 0; i < config->devices_count; i++) {
        device_info_t* device = &config->devices[i];
        char* state = "Unknown";
        switch (device->state) {
        case UNKNOWN:
            state = "unknown";
            break;
        case OFFLINE:
            state = "offline";
            break;
        case DEVICE:
            state = "device";
            break;
        case BOOTLOADER:
            state = "bootloader";
            break;
        }

        fprintf(stdout, "%10s %1s %s", state, device->ops ? " " : "*", device->nodename);
        if (device->inet6_addr.sin6_scope_id != 0) {
            fprintf(stdout, " (%s/%d)", device->inet6_addr_s, device->inet6_addr.sin6_scope_id);
        }
        if (device->state == BOOTLOADER) {
            fprintf(stdout, " [Booloader version 0x%08X listening on %d]", device->bootloader_version, device->bootloader_port);
        }
        fprintf(stdout, "\n");
    }
}

static void add_device(configuration_t* config, const char* nodename) {
    if (config->has_device(config, nodename)) {
        fprintf(stderr, "Device '%s' already added.\n", nodename);
        return;
    }
    device_info_t* new_device = &config->devices[config->devices_count];
    config->devices_count++;
    strncpy(new_device->nodename, nodename, MAX_NODENAME);
    new_device->ops |= OPS_ADD;
    config->save(config);
    printf("Device '%s' added.\n", nodename);
}

static void remove_device(configuration_t* config, const char* nodename) {
    device_info_t* device = config->get_device(config, nodename);
    if (!device) {
        fprintf(stderr, "Device '%s' already removed.\n", nodename);
        return;
    }
    device->ops |= OPS_REMOVE;
    config->save(config);
    printf("Device '%s' removed.\n", nodename);
}

int main(int argc, char** argv) {
    const char* appname = argv[0];
    char* ifname = NULL;
    char* command = NULL;
    configuration_t config;

    while (argc > 1) {
        if (argv[1][0] != '-') {
            if (!command) {
                command = argv[1];
            } else {
                break;
            }
        } else if (!strcmp(argv[1], "-i")) {
            if (argc <= 1) {
                fprintf(stderr, "'-i' option requires an argument (interface name)\n");
                return -1;
            }
            ifname = argv[2];
            argc--;
            argv++;
        }
        argc--;
        argv++;
    }

    if (!command) {
        usage(appname);
    }

    if (!load_configuration(&config)) {
        fprintf(stderr, "Failed to load configuration\n");
        return -1;
    }

    if (!strncmp("devices", command, 7)) {
        list_devices(&config);
    } else if (!strncmp("add", command, 3)) {
        if (argc <= 1) {
            fprintf(stderr, "'add' command requires an argument (nodename)\n");
            return -1;
        }
        add_device(&config, argv[1]);
    } else if (!strncmp("remove", command, 3)) {
        if (argc <= 1) {
            fprintf(stderr, "'remove' command requires an argument (nodename)\n");
            return -1;
        }
        remove_device(&config, argv[1]);
    } else {
        usage(appname);
    }

    return 0;
}
