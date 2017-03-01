// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>

#include <magenta/netboot.h>

#include "configuration.h"

#define MAXSIZE MAX_NODENAME

typedef struct {
    struct nbmsg_t hdr;
    uint8_t data[MAXSIZE];
} msg;

struct sockaddr_in6;

// Returns whether discovery should continue or not.
typedef bool (*on_device_cb)(device_info_t* device, void* cookie);
int netboot_discover(unsigned port, const char* ifname, on_device_cb callback, void* cookie);

int netboot_open(const char* hostname, const char* ifname);

int netboot_txn(int s, msg* in, msg* out, int outlen);
