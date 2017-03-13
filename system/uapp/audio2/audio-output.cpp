// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <magenta/assert.h>
#include <magenta/device/audio2.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <mxtl/algorithm.h>
#include <mxio/io.h>
#include <stdio.h>
#include <string.h>

#include "audio-output.h"

template <typename ReqType, typename RespType>
mx_status_t DoCall(mx_handle_t channel,
                   const ReqType& req,
                   RespType*      resp,
                   mx_handle_t*   req_handles     = nullptr,
                   uint32_t       req_handle_cnt  = 0,
                   mx_handle_t*   resp_handles    = nullptr,
                   uint32_t       resp_handle_cnt = 0) {
    constexpr mx_time_t CALL_TIMEOUT = 100000000u;
    mx_channel_call_args_t args;

    args.wr_bytes       = const_cast<ReqType*>(&req);
    args.wr_num_bytes   = sizeof(ReqType);
    args.wr_handles     = req_handles,
    args.wr_num_handles = req_handle_cnt;
    args.rd_bytes       = resp;
    args.rd_num_bytes   = sizeof(RespType);
    args.rd_handles     = resp_handles;
    args.rd_num_handles = resp_handle_cnt;

    uint32_t bytes, handles;
    mx_status_t read_status, write_status;

    write_status = mx_channel_call(channel, 0, CALL_TIMEOUT, &args, &bytes, &handles, &read_status);

    if (write_status != NO_ERROR) {
        if (write_status == ERR_CALL_FAILED) {
            printf("Cmd read failure (cmd %04x, res %d)\n", req.hdr.cmd, read_status);
            return read_status;
        } else {
            printf("Cmd write failure (cmd %04x, res %d)\n", req.hdr.cmd, write_status);
            return write_status;
        }
    }

    if (bytes != sizeof(RespType)) {
        printf("Unexpected response size (got %u, expected %zu)\n", bytes, sizeof(RespType));
        return ERR_INTERNAL;
    }

    return resp->result;
}

AudioOutput::~AudioOutput() {
    if (rb_vmo_ != MX_HANDLE_INVALID)
        mx_handle_close(rb_vmo_);

    if (rb_ch_ != MX_HANDLE_INVALID)
        mx_handle_close(rb_ch_);

    if (stream_ch_ != MX_HANDLE_INVALID)
        mx_handle_close(stream_ch_);
}

mx_status_t AudioOutput::Open(const char* stream_name) {
    if (stream_ch_ != MX_HANDLE_INVALID)
        return ERR_BAD_STATE;

    int fd = ::open(stream_name, O_RDONLY);
    if (fd < 0) {
        printf("Failed to open \"%s\" (res %d)\n", stream_name, fd);
        return fd;
    }

    ssize_t res = ::mxio_ioctl(fd, AUDIO2_IOCTL_GET_CHANNEL,
                               nullptr, 0,
                               &stream_ch_, sizeof(stream_ch_));
    ::close(fd);

    if (res != NO_ERROR) {
        printf("Failed to obtain channel (res %zd)\n", res);
        return static_cast<mx_status_t>(res);
    }

    return NO_ERROR;
}

mx_status_t AudioOutput::SetFormat(uint32_t frames_per_second,
                                   uint16_t channels,
                                   Audio2BitPacking packing) {
    if ((stream_ch_ == MX_HANDLE_INVALID) || (rb_ch_ != MX_HANDLE_INVALID))
        return ERR_BAD_STATE;

    switch (packing) {
    case AUDIO2_BIT_PACKING_8BIT:
        sample_size_ = 1;
        break;

    case AUDIO2_BIT_PACKING_16BIT_LE:
    case AUDIO2_BIT_PACKING_16BIT_BE:
        sample_size_ = 2;
        break;

    case AUDIO2_BIT_PACKING_20BIT_PACKED_LE:
    case AUDIO2_BIT_PACKING_20BIT_PACKED_BE:
        return ERR_NOT_SUPPORTED;

    case AUDIO2_BIT_PACKING_24BIT_PACKED_LE:
    case AUDIO2_BIT_PACKING_24BIT_PACKED_BE:
        sample_size_ = 3;
        break;

    case AUDIO2_BIT_PACKING_20BIT_IN32_LE:
    case AUDIO2_BIT_PACKING_20BIT_IN32_BE:
    case AUDIO2_BIT_PACKING_24BIT_IN32_LE:
    case AUDIO2_BIT_PACKING_24BIT_IN32_BE:
    case AUDIO2_BIT_PACKING_32BIT_LE:
    case AUDIO2_BIT_PACKING_32BIT_BE:
    case AUDIO2_BIT_PACKING_32BIT_FLOAT:
        sample_size_ = 4;
        break;
    }

    channel_cnt_ = channels;
    frame_sz_    = channels * sample_size_;
    frame_rate_  = frames_per_second;

    Audio2StreamSetFmtReq  req;
    Audio2StreamSetFmtResp resp;
    req.hdr.cmd            = AUDIO2_STREAM_CMD_SET_FORMAT;
    req.hdr.transaction_id = 1;
    req.frames_per_second  = frames_per_second;
    req.channels           = channels;
    req.packing            = packing;

    mx_status_t res = DoCall(stream_ch_, req, &resp, nullptr, 0, &rb_ch_, 1);
    if (res != NO_ERROR) {
        printf("Failed to set format %uHz %hu-Ch %s (res %d)\n",
                frames_per_second, channels, Audio2BitPackingToString(packing), res);
    }

    return res;
}

mx_status_t AudioOutput::SetBuffer(uint32_t frames_per_irq, uint32_t irqs_per_ring) {
    if(!frames_per_irq || !irqs_per_ring)
        return ERR_INVALID_ARGS;

    if ((rb_ch_ == MX_HANDLE_INVALID) || (rb_vmo_ != MX_HANDLE_INVALID))
        return ERR_BAD_STATE;

    rb_sz_ = frame_sz_ * frames_per_irq * irqs_per_ring;

    // Allocate the VMO buffer.
    //
    // TODO(johngro) : How do we ensure that this is aligned properly?  How do
    // we make sure that there are pages pinned underneath this VMO?
    mx_status_t res;
    res = mx_vmo_create(rb_sz_, 0, &rb_vmo_);
    if (res != NO_ERROR) {
        printf("Failed to create %u byte VMO for ring buffer (res %d)\n", rb_sz_, res);
        return res;
    }

    res = mx_vmo_op_range(rb_vmo_, MX_VMO_OP_COMMIT, 0, rb_sz_, nullptr, 0);
    if (res != NO_ERROR) {
        printf("Failed to commit pages for %u bytes in ring buffer VMO (res %d)\n", rb_sz_, res);
        return res;
    }

    // TODO(johngro) : How do I specify the cache policy for this mapping?
    res = mx_vmar_map(mx_vmar_root_self(), 0u,
                      rb_vmo_, 0u, rb_sz_,
                      MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                      reinterpret_cast<uintptr_t*>(&rb_virt_));
    if (res != NO_ERROR) {
        printf("Failed to map ring buffer VMO (res %d)\n", res);
        return res;
    }

    memset(rb_virt_, 0, rb_sz_);

    // Copy the ring buffer VMO and send it to the driver over the ring buffer
    // channel.
    //
    // TODO(johngro) : Restrict the rights on this VMO to the minimum needed.
    mx_handle_t driver_vmo_handle;
    res = mx_handle_duplicate(rb_vmo_, MX_RIGHT_SAME_RIGHTS, &driver_vmo_handle);
    if (res != NO_ERROR) {
        printf("Failed to duplicate VMO handle (res %d)\n", res);
        return res;
    }

    Audio2RBSetBufferReq  req;
    Audio2RBSetBufferResp resp;
    req.hdr.cmd                = AUDIO2_RB_CMD_SET_BUFFER;
    req.hdr.transaction_id     = 1;
    req.ring_buffer_bytes      = rb_sz_;
    req.notifications_per_ring = irqs_per_ring;

    res = DoCall(rb_ch_, req, &resp, &driver_vmo_handle, 1);
    if (res != NO_ERROR) {
        printf("Failed to set driver ring buffer VMO (res %d)\n", res);
        return res;
    }

    return NO_ERROR;
}

mx_status_t AudioOutput::StartRingBuffer() {
    if (rb_ch_ == MX_HANDLE_INVALID)
        return ERR_BAD_STATE;

    Audio2RBStartReq  req;
    Audio2RBStartResp resp;

    req.hdr.cmd = AUDIO2_RB_CMD_START;
    req.hdr.transaction_id = 1;

    return DoCall(rb_ch_, req, &resp);
}

mx_status_t AudioOutput::StopRingBuffer() {
    if (rb_ch_ == MX_HANDLE_INVALID)
        return ERR_BAD_STATE;

    Audio2RBStopReq  req;
    Audio2RBStopResp resp;

    req.hdr.cmd = AUDIO2_RB_CMD_STOP;
    req.hdr.transaction_id = 1;

    return DoCall(rb_ch_, req, &resp);
}

mx_status_t AudioOutput::Play(AudioSource& source) {
    mx_status_t res;

    if (source.finished())
        return NO_ERROR;

    AudioSource::Format format;
    res = source.GetFormat(&format);
    if (res != NO_ERROR) {
        printf("Failed to get source's format (res %d)\n", res);
        return res;
    }

    res = SetFormat(format.frame_rate, format.channels, format.sample_format);
    if (res != NO_ERROR) {
        printf("Failed to set source format [%u Hz, %hu Chan, %08x fmt] (res %d)\n",
                format.frame_rate, format.channels, format.sample_format, res);
        return res;
    }

    // ALSA under QEMU required huge buffers.
    //
    // TODO(johngro) : Make sure that we represent this somehow in the ring
    // buffer interface.
    res = SetBuffer(480 * 20, 3);
    if (res != NO_ERROR) {
        printf("Failed to set output format (res %d)\n", res);
        return res;
    }

    memset(rb_virt_, 0, rb_sz_);

    auto buf = reinterpret_cast<uint8_t*>(rb_virt_);
    uint32_t rd, wr;
    uint32_t playout_rd, playout_amt;
    bool started = false;
    rd = wr = 0;
    playout_rd = playout_amt = 0;

    while (true) {
        uint32_t bytes_read, junk;
        Audio2RBPositionNotify pos_notif;
        mx_signals_t sigs;

        // Top up the buffer
        uint32_t todo;
        uint32_t space = (rb_sz_ + rd - wr - 1) % rb_sz_;
        DEBUG_ASSERT(space < rb_sz_);

        for (uint32_t i = 0; i < 2; ++i) {
            todo = mxtl::min(space, rb_sz_ - wr);
            if (source.finished()) {
                memset(buf + wr, 0, todo);
                wr += todo;
            } else {
                uint32_t done;
                res = source.PackFrames(buf + wr, mxtl::min(space, rb_sz_ - wr), &done);
                if (res != NO_ERROR) {
                    printf("Error packing frames (res %d)\n", res);
                    break;
                }
                wr += done;

                if (source.finished()) {
                    playout_rd  = rd;
                    playout_amt = (rb_sz_ + wr - rd) % rb_sz_;
                }
            }

            if (wr < rb_sz_)
                break;

            DEBUG_ASSERT(wr == rb_sz_);
            wr = 0;
        }

        if (res != NO_ERROR)
            break;

         mx_vmo_op_range(rb_vmo_, MX_VMO_OP_CACHE_CLEAN, 0, rb_sz_, nullptr, 0 );

        // If we have not started yet, do so.
        if (!started) {
            res = StartRingBuffer();
            if (res != NO_ERROR) {
                printf("Failed to start ring buffer!\n");
                break;
            }
            started = true;
        }

        res = mx_object_wait_one(rb_ch_, MX_CHANNEL_READABLE, MX_TIME_INFINITE, &sigs);

        if (res != NO_ERROR) {
            printf("Failed to wait for notificiation (res %d)\n", res);
            break;
        }

        res = mx_channel_read(rb_ch_, 0,
                              &pos_notif, sizeof(pos_notif), &bytes_read,
                              nullptr, 0, &junk);
        if (res != NO_ERROR) {
            printf("Failed to read notification from ring buffer channel (res %d)\n", res);
            break;
        }

        if (bytes_read != sizeof(pos_notif)) {
            printf("Bad size when reading notification from ring buffer channel (%u != %zu)\n",
                   bytes_read, sizeof(pos_notif));
            res = ERR_INTERNAL;
            break;
        }

        if (pos_notif.hdr.cmd != AUDIO2_RB_POSITION_NOTIFY) {
            printf("Unexpected command type when reading notification from ring "
                   "buffer channel (cmd %04x)\n", pos_notif.hdr.cmd);
            res = ERR_INTERNAL;
            break;
        }

        rd = pos_notif.ring_buffer_pos;

        // rd has moved.  If the source has finished and rd has moved at least
        // the playout distance, we are finsihed.
        if (source.finished()) {
            uint32_t dist = (rb_sz_ + rd - playout_rd) % rb_sz_;

            if (dist >= playout_amt)
                break;

            playout_amt -= dist;
            playout_rd   = rd;
        }
    }

    if (res == NO_ERROR) {
        // We have already let the DMA engine catch up, but we still need to
        // wait for the fifo to play out.  For now, just hard code this as
        // 30uSec.
        //
        // TODO: base this on the start time and the number of frames queued
        // instead of just making a number up.
        mx_nanosleep(30000000);
    }

    mx_status_t stop_res = StopRingBuffer();
    if (res == NO_ERROR)
        res = stop_res;

    return res;
}
