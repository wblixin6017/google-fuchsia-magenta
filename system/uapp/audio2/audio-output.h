// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

#include "audio-source.h"

class AudioOutput {
public:
    AudioOutput() { }
    ~AudioOutput();

    mx_status_t Open(const char* stream_name);
    mx_status_t SetFormat(uint32_t frames_per_second, uint16_t channels, Audio2BitPacking packing);
    mx_status_t SetBuffer(uint32_t frames_per_irq, uint32_t irqs_per_ring);
    mx_status_t StartRingBuffer();
    mx_status_t StopRingBuffer();
    mx_status_t Play(AudioSource& source);

private:
    mx_handle_t stream_ch_ = MX_HANDLE_INVALID;
    mx_handle_t rb_ch_     = MX_HANDLE_INVALID;
    mx_handle_t rb_vmo_    = MX_HANDLE_INVALID;

    uint32_t frame_rate_  = 0;
    uint32_t sample_size_ = 0;
    uint32_t channel_cnt_ = 0;
    uint32_t frame_sz_    = 0;
    uint32_t rb_sz_       = 0;
    void*    rb_virt_     = nullptr;
};

