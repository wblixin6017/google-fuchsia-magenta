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
    AUDIO2_RB_CMD_SET_BUFFER     = 0x2000,
    AUDIO2_RB_CMD_START          = 0x2001,
    AUDIO2_RB_CMD_STOP           = 0x2002,

    // Async notifications sent on the ring buffer channel.
    AUDIO2_RB_POSITION_NOTIFY    = 0x3000,
} audio2_cmd_t;

typedef struct audio2_cmd_hdr {
    uint32_t     transaction_id;
    audio2_cmd_t cmd;
} audio2_cmd_hdr_t;

// AUDIO2_STREAM_CMD_SET_FORMAT
typedef enum audio2_bit_packing {
    AUDIO2_BIT_PACKING_8BIT = 1,
    AUDIO2_BIT_PACKING_16BIT_LE,
    AUDIO2_BIT_PACKING_16BIT_BE,
    AUDIO2_BIT_PACKING_20BIT_PACKED_LE,
    AUDIO2_BIT_PACKING_20BIT_PACKED_BE,
    AUDIO2_BIT_PACKING_24BIT_PACKED_LE,
    AUDIO2_BIT_PACKING_24BIT_PACKED_BE,
    AUDIO2_BIT_PACKING_20BIT_IN32_LE,
    AUDIO2_BIT_PACKING_20BIT_IN32_BE,
    AUDIO2_BIT_PACKING_24BIT_IN32_LE,
    AUDIO2_BIT_PACKING_24BIT_IN32_BE,
    AUDIO2_BIT_PACKING_32BIT_LE,
    AUDIO2_BIT_PACKING_32BIT_BE,
    AUDIO2_BIT_PACKING_32BIT_FLOAT,
} audio2_bit_packing_t;

typedef struct audio2_stream_cmd_set_format_req {
    audio2_cmd_hdr_t     hdr;
    uint32_t             frames_per_second;
    audio2_bit_packing_t packing;
    uint16_t             channels;
} audio2_stream_cmd_set_format_req_t;

typedef struct audio2_stream_cmd_set_format_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;

    // Note: Upon success, a channel used to control the audio buffer will also
    // be returned.
} audio2_stream_cmd_set_format_resp_t;


// AUDIO2_RB_CMD_SET_BUFFER
typedef struct audio2_rb_cmd_set_buffer_req {
    audio2_cmd_hdr_t hdr;
    uint32_t         ring_buffer_bytes;
    uint32_t         notifications_per_ring;

    // NOTE: A VMO handle must also be provided by the client.  This is the VMO
    // handle to the ring buffer which the client will use to send/receive data.
    // The handle must be aligned properly, and the pages underneath it must be
    // pinned.
} audio2_rb_cmd_set_buffer_req_t;

typedef struct audio2_rb_cmd_set_buffer_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;
} audio2_rb_cmd_set_buffer_resp_t;

// AUDIO2_RB_CMD_START
typedef struct audio2_rb_cmd_start_req {
    audio2_cmd_hdr_t hdr;
} audio2_rb_cmd_start_req_t;

typedef struct audio2_rb_cmd_start_resp {
    audio2_cmd_hdr_t hdr;
    mx_status_t      result;
    uint64_t         start_ticks;
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
    uint32_t         ring_buffer_pos;
} audio2_rb_position_notify_t;

__END_CDECLS

#ifdef __cplusplus
// C++ style aliases for protocol structures and types.
using Audio2Cmd    = audio2_cmd_t;
using Audio2CmdHdr = audio2_cmd_hdr_t;

// AUDIO2_STREAM_CMD_SET_FORMAT
using Audio2BitPacking       = audio2_bit_packing_t;
using Audio2StreamSetFmtReq  = audio2_stream_cmd_set_format_req_t;
using Audio2StreamSetFmtResp = audio2_stream_cmd_set_format_resp_t;

// AUDIO2_RB_CMD_SET_BUFFER
using Audio2RBSetBufferReq  = audio2_rb_cmd_set_buffer_req_t;
using Audio2RBSetBufferResp = audio2_rb_cmd_set_buffer_resp_t;

// AUDIO2_RB_CMD_START
using Audio2RBStartReq  = audio2_rb_cmd_start_req_t;
using Audio2RBStartResp = audio2_rb_cmd_start_resp_t;

// AUDIO2_RB_CMD_STOP
using Audio2RBStopReq  = audio2_rb_cmd_stop_req_t;
using Audio2RBStopResp = audio2_rb_cmd_stop_resp_t;

// AUDIO2_RB_POSITION_NOTIFY
using Audio2RBPositionNotify = audio2_rb_position_notify_t;

static inline const char* Audio2BitPackingToString(Audio2BitPacking packing) {
    switch (packing) {
    case AUDIO2_BIT_PACKING_8BIT:            return "8BIT";
    case AUDIO2_BIT_PACKING_16BIT_LE:        return "16BIT_LE";
    case AUDIO2_BIT_PACKING_16BIT_BE:        return "16BIT_BE";
    case AUDIO2_BIT_PACKING_20BIT_PACKED_LE: return "20BIT_PACKED_LE";
    case AUDIO2_BIT_PACKING_20BIT_PACKED_BE: return "20BIT_PACKED_BE";
    case AUDIO2_BIT_PACKING_24BIT_PACKED_LE: return "24BIT_PACKED_LE";
    case AUDIO2_BIT_PACKING_24BIT_PACKED_BE: return "24BIT_PACKED_BE";
    case AUDIO2_BIT_PACKING_20BIT_IN32_LE:   return "20BIT_IN32_LE";
    case AUDIO2_BIT_PACKING_20BIT_IN32_BE:   return "20BIT_IN32_BE";
    case AUDIO2_BIT_PACKING_24BIT_IN32_LE:   return "24BIT_IN32_LE";
    case AUDIO2_BIT_PACKING_24BIT_IN32_BE:   return "24BIT_IN32_BE";
    case AUDIO2_BIT_PACKING_32BIT_FLOAT:     return "32BIT_FLOAT";
    default:                                 return "<unknown>";
    }
}

#endif  // __cplusplus
