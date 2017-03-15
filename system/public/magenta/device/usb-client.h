// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdint.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

__BEGIN_CDECLS

// sets the client controller's connected state
// call with in_len = sizeof(int)
#define IOCTL_USB_CLIENT_SET_CONNNECTED     IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_CLIENT, 1)

IOCTL_WRAPPER_IN(ioctl_usb_client_set_connected, IOCTL_USB_CLIENT_SET_CONNNECTED, int);

__END_CDECLS
