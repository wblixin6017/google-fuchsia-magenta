// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// converts a USB endpoint adddress to an index from 0 to 31
uint8_t ep_addr_to_index(uint8_t ep_address);
