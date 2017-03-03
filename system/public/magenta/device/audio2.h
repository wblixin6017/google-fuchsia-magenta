// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/device/ioctl.h>
#include <magenta/types.h>

#define AUDIO2_IOCTL_GET_CHANNEL IOCTL(IOCTL_KIND_GET_HANDLE, 0xFE, 0x00)

// When communicating with an Audio2 driver using mx_channel_call, do not use
// the AUDIO2_INVALID_TRANSACTION_ID as your message's transaction ID.  It is
// reserved for async notifications sent from the driver to the application.
#define AUDIO2_INVALID_TRANSACTION_ID ((uint32_t)0)

__BEGIN_CDECLS

typedef enum audio2_cmd {
    // Commands sent on the stream channel
    AUDIO2_STREAM_CMD_SET_FORMAT = 0x1000,

    // Commands sent on the ring buffer channel
    AUDIO2_RB_CMD_GET_FIFO_DEPTH = 0x2000,
    AUDIO2_RB_CMD_GET_BUFFER     = 0x2001,
    AUDIO2_RB_CMD_START          = 0x2002,
    AUDIO2_RB_CMD_STOP           = 0x2003,

    // Async notifications sent on the ring buffer channel.
    AUDIO2_RB_POSITION_NOTIFY    = 0x3000,
} audio2_cmd_t;

typedef struct audio2_cmd_hdr {
    uint32_t     transaction_id;
    audio2_cmd_t cmd;
} audio2_cmd_hdr_t;

// AUDIO2_STREAM_CMD_SET_FORMAT
//
// Bitfield which describes audio sample format as they reside in memory.
//
// ++ With the exception of FORMAT_BITSTREAM, samples are always assumed to
//    use linear PCM encoding.  BITSTREAM is used for transporting compressed
//    audio encodings (such as AC3, DTS, and so on) over a digital interconnect
//    to a decoder device somewhere outside of the system.
// ++ Be default, multi-byte sample formats are assumed to use host-endianness.
//    If the INVERT_ENDIAN flag is set on the format, the format uses the
//    opposie of host endianness.  eg. A 16 bit little endian PCM audio format
//    would have the INVERT_ENDIAN flag set on it in a when used on a big endian
//    host.  The INVERT_ENDIAN flag has no effect on COMPRESSED, 8BIT or FLOAT
//    encodings.
// ++ The 32BIT_FLOAT encoding uses specifically the IEE 754 floating point
//    representation.
// ++ Be default, non-floating point PCM encodings are assumed expressed using
//    2s compliment signed integers.  eg. the bit values for a 16 bit PCM sample
//    format would range from [0x8000, 0x7FFF] with 0x0000 represting zero
//    speaker deflection.  If the UNSIGNED flag is set on the format, the bit
//    values would range from [0x0000, 0xFFFF] with 0x8000 representing zero
//    deflection.
// ++ When used to set formats, exactly one non-flag bit *must* be set.
// ++ When used to describe supported formats, and number of non-flag bits may
//    be set.  Flags (when present) apply to all of the relevant non-flag bits
//    in the bitfield.  eg.  If a stream supports COMPRESSED, 16BIT and
//    32BIT_FLOAT, and the UNSIGNED bit is set, it applies only to the 16BIT
//    format.
// ++ When encoding a smaller sample size in a larger container (eg 20 or 24bit
//    in 32), the most significant bits of the 32 bit container are used while
//    the least significant bits should be zero.  eg. a 20 bit sample would be
//    mapped onto the range [12,32] of the 32 bit container.
//
//    TODO(johngro) : can we make the claim that the LSBs will be ignored, or do
//    we have to require that they be zero?
//
//    TODO(johngro) : describe what 20-bit packed audio looks like in memory.
//    Does it need to have an even number of channels in the overall format?
//    Should we strike it from this list if we cannot find a piece of hardware
//    which demands this format in memory?
typedef enum audio2_sample_format {
    AUDIO2_SAMPLE_FORMAT_BITSTREAM    = (1u << 0),
    AUDIO2_SAMPLE_FORMAT_8BIT         = (1u << 1),
    AUDIO2_SAMPLE_FORMAT_16BIT        = (1u << 2),
    AUDIO2_SAMPLE_FORMAT_20BIT_PACKED = (1u << 4),
    AUDIO2_SAMPLE_FORMAT_24BIT_PACKED = (1u << 5),
    AUDIO2_SAMPLE_FORMAT_20BIT_IN32   = (1u << 6),
    AUDIO2_SAMPLE_FORMAT_24BIT_IN32   = (1u << 7),
    AUDIO2_SAMPLE_FORMAT_32BIT        = (1u << 8),
    AUDIO2_SAMPLE_FORMAT_32BIT_FLOAT  = (1u << 9),

    AUDIO2_SAMPLE_FORMAT_FLAG_UNSIGNED      = (1u << 30),
    AUDIO2_SAMPLE_FORMAT_FLAG_INVERT_ENDIAN = (1u << 31),
    AUDIO2_SAMPLE_FORMAT_FLAG_MASK = AUDIO2_SAMPLE_FORMAT_FLAG_UNSIGNED |
                                     AUDIO2_SAMPLE_FORMAT_FLAG_INVERT_ENDIAN,
} audio2_sample_format_t;

typedef struct audio2_stream_cmd_set_format_req {
    audio2_cmd_hdr_t       hdr;
    uint32_t               frames_per_second;
    audio2_sample_format_t sample_format;
    uint16_t               channels;
} audio2_stream_cmd_set_format_req_t;

typedef struct audio2_stream_cmd_set_format_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;

    // Note: Upon success, a channel used to control the audio buffer will also
    // be returned.
} audio2_stream_cmd_set_format_resp_t;

// AUDIO2_RB_CMD_GET_FIFO_DEPTH
//
// TODO(johngro) : Is calling this "FIFO" depth appropriate?  Should it be some
// direction neutral form of something like "max-read-ahead-amount" or something
// instead?
typedef struct audio2_rb_cmd_get_fifo_depth_req {
    audio2_cmd_hdr_t hdr;
} audio2_rb_cmd_get_fifo_depth_req_t;

typedef struct audio2_rb_cmd_get_fifo_depth_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;

    // A representation (in bytes) of how far ahead audio hardware may read
    // into the stream (in the case of output) or may hold onto audio before
    // writing it to memory (in the case of input).
    uint32_t fifo_depth;
} audio2_rb_cmd_get_fifo_depth_resp_t;

// AUDIO2_RB_CMD_GET_BUFFER
typedef struct audio2_rb_cmd_get_buffer_req {
    audio2_cmd_hdr_t hdr;

    // Minimum number of frames of audio the client need allocated for the ring
    // buffer.  Drivers may need to make this buffer larger in order to meet
    // hardware requirement.  Clients *must* use the returned VMOs size (in
    // bytes) to determine the actual size of the ring buffer may not assume
    // that the size of the buffer (as determined by the driver) is exactly the
    // size they requested.  Drivers *must* ensure that the size of the ring
    // buffer is an integral number of audio frames.
    //
    // TODO(johngro) : Is it reasonable to require that driver produce buffers
    // which are an integral number of audio frames in length.  It certainly
    // makes the audio client's life easier (client code never needs to split or
    // re-assemble a frame before processing), but it might make it difficult
    // for some audio hardware to meet its requirements without making the
    // buffer significantly larger than the client asked for.
    uint32_t min_ring_buffer_frames;

    // The number of position update notifications (audio2_rb_position_notify_t)
    // the client would like the driver to send per cycle through the ring
    // buffer.  Drivers should attempt to space the notifications as uniformly
    // throughout the ring as their hardware design allows, but clients may not
    // rely on perfectly uniform spacing of the update notifications.  Client's
    // are not required to request any notifications at all and may choose to
    // run using only start time and FIFO depth information to determine the
    // driver's playout or capture position.
    uint32_t notifications_per_ring;
} audio2_rb_cmd_get_buffer_req_t;

typedef struct audio2_rb_cmd_get_buffer_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;

    // NOTE: If result == NO_ERROR, a VMO handle representing the ring buffer to
    // be used will be returned as well.  Clients may map this buffer with
    // read-write permissions in the case of an output stream, or read-only
    // permissions in the case of an input stream.  The size of the VMO
    // indicates where the wrap point of the ring (in bytes) is located in the
    // VMO.  This size *must* always be an integral number of audio frames.
    //
    // TODO(johngro) : Should we provide some indication of whether or not this
    // memory is being used directly for HW DMA and may need explicit cache
    // flushing/invalidation?
} audio2_rb_cmd_get_buffer_resp_t;

// AUDIO2_RB_CMD_START
typedef struct audio2_rb_cmd_start_req {
    audio2_cmd_hdr_t hdr;
} audio2_rb_cmd_start_req_t;

typedef struct audio2_rb_cmd_start_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;

    // Nominal time at which the first frame of audio started to be clocked out
    // to the codec as measured by mx_ticks_get().
    //
    // TODO(johngro) : Redefine this allow it to be an arbitrary 'audio stream
    // clock' instead of the mx_ticks_get() clock.  If the stream clock is made
    // to count in audio frames since start, then this start_ticks can be
    // replaced with the terms for a segment of a piecewise linear
    // transformation which can be subsequently updated via notifications sent
    // by the driver in the case that the audio hardware clock is rooted in a
    // different oscillator from the system's tick counter.  Clients can then
    // use this transformation either to control the rate of consumption of
    // input streams, or to determine where to sample in the input stream
    // to effect clock correction.
    uint64_t start_ticks;
} audio2_rb_cmd_start_resp_t;

// AUDIO2_RB_CMD_STOP
typedef struct audio2_rb_cmd_stop_req {
    audio2_cmd_hdr_t hdr;
} audio2_rb_cmd_stop_req_t;

typedef struct audio2_rb_cmd_stop_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;
} audio2_rb_cmd_stop_resp_t;

// AUDIO2_RB_POSITION_NOTIFY
typedef struct audio2_rb_position_notify {
    audio2_cmd_hdr_t hdr;

    // The current position (in bytes) of the driver/hardware's read (output) or
    // write (input) pointer in the ring buffer.
    uint32_t ring_buffer_pos;
} audio2_rb_position_notify_t;

__END_CDECLS
