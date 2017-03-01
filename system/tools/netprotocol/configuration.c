// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L

#include "configuration.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* configuration_path = "/.config/magenta/devices";

static FILE* open_configuration() {
    // TODO check if directory exists first
    char* home = getenv("HOME");
    size_t len = strlen(home) + strlen(configuration_path);
    char* path = (char*)malloc(len);
    sprintf(path, "%s%s", home, configuration_path);
    path[len] = 0;
    FILE* f = fopen(path, "r+");
    free(path);
    return f;
}

static bool has_device(configuration_t* configuration, const char* nodename) {
    for (uint32_t i = 0; i < configuration->devices_count; ++i) {
        if (!strncmp(configuration->devices[i].nodename, nodename, MAX_NODENAME)) {
            return true;
        }
    }
    return false;
}

static device_info_t* get_device(configuration_t* configuration, const char* nodename) {
    for (uint32_t i = 0; i < configuration->devices_count; ++i) {
        if (!strncmp(configuration->devices[i].nodename, nodename, MAX_NODENAME)) {
            return &configuration->devices[i];
        }
    }
    return NULL;
}

static device_info_t* add_device(configuration_t* configuration, device_info_t* device) {
    device_info_t* known_device = configuration->get_device(configuration, device->nodename);
    if (!known_device) {
        known_device = &configuration->devices[configuration->devices_count];
        configuration->devices_count++;
        strncpy(known_device->nodename, device->nodename, MAX_NODENAME);
        // known_device->ops |= OPS_DISCOVERED;
    }
    strncpy(known_device->inet6_addr_s, device->inet6_addr_s, INET6_ADDRSTRLEN);
    memcpy(&known_device->inet6_addr, &device->inet6_addr, sizeof(known_device->inet6_addr));
    known_device->state = device->state;
    known_device->bootloader_port = device->bootloader_port;
    known_device->bootloader_version = device->bootloader_version;
    return known_device;
}

static bool save(configuration_t* configuration) {
    FILE* f = open_configuration();
    if (!f)
        return false;
    // TODO improve so we don't rewrite the entire file every time
    for (uint32_t i = 0; i < configuration->devices_count; ++i) {
        if (!configuration->devices[i].ops || configuration->devices[i].ops & OPS_ADD) {
            char tmp[MAX_NODENAME + 2];
            snprintf(tmp, MAX_NODENAME + 1, "%s\n", configuration->devices[i].nodename);
            fwrite(tmp, strlen(tmp), 1, f);
        }
    }
    long length = ftell(f);
    ftruncate(fileno(f), length);
    fclose(f);
    return true;
}

bool load_configuration(configuration_t* config) {
    config->devices_count = 0;
    config->has_device = has_device;
    config->get_device = get_device;
    config->add_device = add_device;
    config->save = save;

    FILE* f = open_configuration();
    if (!f) {
        return true;
    }

    char buffer[4096];
    while (!feof(f)) {
        if (fgets(buffer, sizeof(buffer), f) != NULL) {
            // Trim newline
            char* endptr = buffer + strlen(buffer) - 1;
            while (*endptr == '\n' || *endptr == '\r') {
                *endptr = 0;
                if (--endptr == buffer)
                    break;
            }
            if (buffer[0] == 0)
                continue;
            if (config->has_device(config, buffer))
                continue;
            strncpy(config->devices[config->devices_count].nodename, buffer, MAX_NODENAME);
            config->devices[config->devices_count].state = OFFLINE;
            config->devices[config->devices_count].ops = 0;
            config->devices_count++;
        }
    }

    fclose(f);
    return true;
}
