// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_NODENAME 1024
#define MAX_DEVICES 255

#define OPS_DISCOVERED 0x01
#define OPS_ADD 0x02
#define OPS_REMOVE 0x04

typedef enum device_state {
  UNKNOWN, OFFLINE, DEVICE, BOOTLOADER,
} device_state_t;

typedef struct device_info {
  char nodename[MAX_NODENAME];
  char inet6_addr_s[INET6_ADDRSTRLEN];
  struct sockaddr_in6 inet6_addr;
  device_state_t state;
  uint32_t bootloader_version;
  uint16_t bootloader_port;
  uint32_t ops;
} device_info_t;

typedef struct configuration configuration_t;

struct configuration {
  device_info_t devices[MAX_DEVICES];
  uint16_t devices_count;

  bool(*has_device)(configuration_t* configuration, const char* nodename);
  device_info_t*(*get_device)(configuration_t* configuration, const char* nodename);
  device_info_t* (*add_device)(configuration_t* configuration, device_info_t* device);
  bool (*save)(configuration_t* configuration);
};

bool load_configuration(configuration_t* configuration);
